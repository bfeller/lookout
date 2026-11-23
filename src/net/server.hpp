// Minimal HTTP server for the viewer page, the MJPEG video stream and the MP3 audio stream.
// Every route requires the secret token (?k=...).
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "media/media.hpp"

namespace lk {

struct ServerHooks {
    std::function<std::string()> token;
    std::function<std::string()> statusJson;
    // Apply a setting from the page ("quality", "rotate", "gain"); returns false if rejected.
    std::function<bool(const std::string& key, int value)> set;
};

class Server {
public:
    Server(VideoSource& video, AudioSource& audio, ServerHooks hooks)
        : video_(video), audio_(audio), hooks_(std::move(hooks)) {}
    ~Server();
    bool start(int port, std::string& error);
    int port() const { return port_; }

    int viewers() const { return viewers_.load(); }      // open video streams (+ pending snapshots)
    int listeners() const { return listeners_.load(); }  // open audio streams
    int64_t lastRequestMs() const { return lastRequest_.load(); }
    // Disconnect every open stream (used after issuing a new access link).
    void revokeAll() { ++generation_; }

private:
    void acceptLoop();
    void handle(int fd);
    void streamVideo(int fd);
    void streamAudio(int fd);

    VideoSource& video_;
    AudioSource& audio_;
    ServerHooks hooks_;
    int listenFd_ = -1;
    int port_ = 0;
    std::thread acceptThread_;
    std::atomic<int> viewers_{0}, listeners_{0}, clients_{0};
    std::atomic<int64_t> lastRequest_{0};
    std::atomic<int> generation_{0};
};

}  // namespace lk
