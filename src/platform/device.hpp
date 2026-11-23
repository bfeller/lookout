// Cardputer Zero device helpers: keyboard, battery, backlight, network address,
// persistent settings and child processes.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lk {

int64_t nowMs();

enum class Key : uint16_t { None = 0, Esc, LongEsc, Enter, Backspace, Space, Up, Down, Left, Right, Char, Any };
struct KeyEvent {
    Key key = Key::None;
    char ch = 0;  // lowercase letter or digit for Key::Char
};

class Keyboard {
public:
    ~Keyboard();
    bool open();
    KeyEvent poll();  // non-blocking

private:
    int fd_ = -1;
    int64_t escDownMs_ = -1;
    bool escLongFired_ = false;
};

int batteryPercent();                 // -1 if unknown
std::string localIp();                // first non-loopback IPv4, "" if offline

class Backlight {
public:
    void init();                       // remembers the current level
    void set(bool on);
    void restore();
    bool on() const { return on_; }

private:
    std::string path_;
    int level_ = -1;
    bool on_ = true;
};

// Persistent settings in ~/.config/lookout/settings (key=value).
struct Settings {
    std::string token;   // secret in the viewing URL
    int quality = 1;     // 0 low, 1 medium, 2 high
    int micGain = 1;     // 0 +12 dB, 1 +24 dB, 2 +36 dB
    bool micEnabled = true;
    bool rotate180 = false;
    int port = 8080;

    void load();
    void save() const;
    static std::string newToken();
};

// A child process whose stdout we read. The child is killed if we die.
class Child {
public:
    ~Child() { stop(); }
    bool start(const std::vector<std::string>& args);
    void kill();     // terminate and reap, but keep our end of the pipe open
    void stop();     // kill() and close the pipe
    bool running();
    int fd() const { return fd_; }

private:
    pid_t pid_ = -1;
    int fd_ = -1;
};

}  // namespace lk
