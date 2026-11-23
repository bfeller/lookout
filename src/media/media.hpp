// Camera (MJPEG from rpicam-vid) and microphone (MP3 from ffmpeg) sources.
// Each runs a child process plus a reader thread and publishes data to any number of consumers.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "platform/device.hpp"

namespace lk {

struct VideoConfig {
    int width = 1280, height = 720, fps = 15, quality = 70;
    bool rotate180 = false;
    bool operator==(const VideoConfig& o) const
    {
        return width == o.width && height == o.height && fps == o.fps && quality == o.quality &&
               rotate180 == o.rotate180;
    }
};
VideoConfig qualityPreset(int level, bool rotate180);
const char* qualityName(int level);

using Frame = std::shared_ptr<const std::string>;

class VideoSource {
public:
    ~VideoSource() { stop(); }
    bool start(const VideoConfig& cfg);
    void stop();
    bool running() const { return running_; }
    const VideoConfig& config() const { return cfg_; }

    Frame latest(uint64_t* seq = nullptr) const;
    // Wait (up to timeoutMs) for a frame newer than `after`. Returns null on timeout.
    Frame waitNewer(uint64_t after, uint64_t& seq, int timeoutMs) const;
    double fps() const { return fps_.load(); }

private:
    void reader();

    Child child_;
    std::thread thread_;
    VideoConfig cfg_;
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    mutable std::condition_variable cv_;
    Frame frame_;
    uint64_t seq_ = 0;
    std::atomic<double> fps_{0};
};

class AudioSource {
public:
    ~AudioSource() { stop(); }
    bool start(int gainDb);
    void stop();
    bool running() const { return running_; }
    int gain() const { return gain_; }

    // Append every chunk newer than `cursor` to `out`; waits up to timeoutMs for data.
    // A new reader should pass cursor = 0 to start at the live edge.
    bool read(uint64_t& cursor, std::string& out, int timeoutMs) const;

private:
    void reader();

    Child child_;
    std::thread thread_;
    int gain_ = 24;
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    mutable std::condition_variable cv_;
    std::deque<std::pair<uint64_t, std::string>> chunks_;
    uint64_t seq_ = 0;
};

}  // namespace lk
