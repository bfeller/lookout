#include "media/media.hpp"

#include <unistd.h>

#include <chrono>

namespace lk {

VideoConfig qualityPreset(int level, bool rotate180)
{
    VideoConfig c;
    switch (level) {
    case 0: c = {640, 480, 15, 70, false}; break;
    case 2: c = {1920, 1080, 10, 75, false}; break;
    default: c = {1280, 720, 15, 70, false}; break;
    }
    c.rotate180 = rotate180;
    return c;
}

const char* qualityName(int level)
{
    switch (level) {
    case 0: return "Low 480p";
    case 2: return "High 1080p";
    default: return "Medium 720p";
    }
}

// ------------------------------------------------------------------ video
bool VideoSource::start(const VideoConfig& cfg)
{
    stop();
    cfg_ = cfg;
    std::vector<std::string> args = {"rpicam-vid", "-n", "-t", "0", "--codec", "mjpeg",
                                     "--quality", std::to_string(cfg.quality),
                                     "--width", std::to_string(cfg.width), "--height", std::to_string(cfg.height),
                                     "--framerate", std::to_string(cfg.fps), "--flush", "-o", "-"};
    // The Cardputer Zero's camera module is mounted upside down, so "upright" needs a 180
    // degree sensor rotation; the user's rotate setting flips relative to upright.
    if (!cfg.rotate180) {
        args.push_back("--rotation");
        args.push_back("180");
    }
    if (!child_.start(args)) return false;
    running_ = true;
    thread_ = std::thread(&VideoSource::reader, this);
    return true;
}

void VideoSource::stop()
{
    child_.kill();  // reader sees EOF and exits
    if (thread_.joinable()) thread_.join();
    child_.stop();
    running_ = false;
    fps_ = 0;
    std::lock_guard<std::mutex> lock(mu_);
    frame_.reset();
}

void VideoSource::reader()
{
    std::string buf;
    buf.reserve(512 * 1024);
    char tmp[64 * 1024];
    int64_t windowStart = nowMs();
    int windowFrames = 0;
    for (;;) {
        const ssize_t n = ::read(child_.fd(), tmp, sizeof tmp);
        if (n <= 0) break;
        buf.append(tmp, static_cast<size_t>(n));
        // rpicam-vid writes back-to-back JPEGs: SOI (FF D8) ... EOI (FF D9).
        for (;;) {
            const size_t soi = buf.find("\xFF\xD8", 0, 2);
            if (soi == std::string::npos) {
                buf.clear();
                break;
            }
            if (soi > 0) buf.erase(0, soi);
            const size_t eoi = buf.find("\xFF\xD9", 2, 2);
            if (eoi == std::string::npos) break;
            auto frame = std::make_shared<const std::string>(buf.substr(0, eoi + 2));
            buf.erase(0, eoi + 2);
            {
                std::lock_guard<std::mutex> lock(mu_);
                frame_ = std::move(frame);
                ++seq_;
            }
            cv_.notify_all();
            ++windowFrames;
            const int64_t now = nowMs();
            if (now - windowStart >= 2000) {
                fps_ = windowFrames * 1000.0 / (now - windowStart);
                windowStart = now;
                windowFrames = 0;
            }
        }
        if (buf.size() > 8 * 1024 * 1024) buf.clear();  // never let garbage grow
    }
    running_ = false;
    cv_.notify_all();
}

Frame VideoSource::latest(uint64_t* seq) const
{
    std::lock_guard<std::mutex> lock(mu_);
    if (seq) *seq = seq_;
    return frame_;
}

Frame VideoSource::waitNewer(uint64_t after, uint64_t& seq, int timeoutMs) const
{
    std::unique_lock<std::mutex> lock(mu_);
    // Wait the full timeout even while the camera is stopped: callers use this to pace
    // themselves while the camera starts up.
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return seq_ > after && frame_; });
    if (seq_ <= after || !frame_) return nullptr;
    seq = seq_;
    return frame_;
}

// ------------------------------------------------------------------ audio
bool AudioSource::start(int gainDb)
{
    stop();
    gain_ = gainDb;
    // The Cardputer's microphone is on the left channel only, and quiet: take the left
    // channel, cut rumble, add gain and a limiter so loud sounds don't clip.
    const std::string filter = "pan=mono|c0=c0,highpass=f=120,volume=" + std::to_string(gainDb) +
                               "dB,alimiter=limit=0.9:level=false";
    std::vector<std::string> args = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-fflags", "nobuffer",
                                     "-f", "pulse", "-fragment_size", "2048", "-i", "default",
                                     "-af", filter, "-ar", "32000", "-ac", "1",
                                     "-c:a", "libmp3lame", "-b:a", "64k", "-f", "mp3", "-flush_packets", "1", "-"};
    if (!child_.start(args)) return false;
    running_ = true;
    thread_ = std::thread(&AudioSource::reader, this);
    return true;
}

void AudioSource::stop()
{
    child_.kill();
    if (thread_.joinable()) thread_.join();
    child_.stop();
    running_ = false;
    std::lock_guard<std::mutex> lock(mu_);
    chunks_.clear();
}

void AudioSource::reader()
{
    char tmp[4096];
    for (;;) {
        const ssize_t n = ::read(child_.fd(), tmp, sizeof tmp);
        if (n <= 0) break;
        {
            std::lock_guard<std::mutex> lock(mu_);
            chunks_.emplace_back(++seq_, std::string(tmp, static_cast<size_t>(n)));
            while (chunks_.size() > 256) chunks_.pop_front();
        }
        cv_.notify_all();
    }
    running_ = false;
    cv_.notify_all();
}

bool AudioSource::read(uint64_t& cursor, std::string& out, int timeoutMs) const
{
    std::unique_lock<std::mutex> lock(mu_);
    if (cursor == 0) cursor = seq_;  // start at the live edge
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return seq_ > cursor; });
    bool any = false;
    for (const auto& [s, data] : chunks_) {
        if (s <= cursor) continue;
        out += data;
        cursor = s;
        any = true;
    }
    return any;
}

}  // namespace lk
