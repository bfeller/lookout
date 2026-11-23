#include "platform/device.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/input.h>
#include <net/if.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace lk {

int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------ keyboard
Keyboard::~Keyboard()
{
    if (fd_ >= 0) ::close(fd_);
}

bool Keyboard::open()
{
    for (const char* p : {"/dev/input/by-path/platform-3f804000.i2c-event", "/dev/input/cardputer-zero-internal",
                          "/dev/input/event3"}) {
        fd_ = ::open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ >= 0) return true;
    }
    return false;
}

KeyEvent Keyboard::poll()
{
    if (escDownMs_ >= 0 && !escLongFired_ && nowMs() - escDownMs_ >= 800) {
        escLongFired_ = true;
        return {Key::LongEsc, 0};
    }
    if (fd_ < 0) return {};
    input_event ev;
    while (::read(fd_, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev)) {
        if (ev.type != EV_KEY) continue;
        if (ev.code == KEY_ESC) {
            if (ev.value == 1) {
                escDownMs_ = nowMs();
                escLongFired_ = false;
            } else if (ev.value == 0) {
                const bool wasLong = escLongFired_;
                escDownMs_ = -1;
                if (!wasLong) return {Key::Esc, 0};
            }
            continue;
        }
        if (ev.value == 0) continue;
        static const char letters[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm";
        switch (ev.code) {
        case KEY_ENTER: return {Key::Enter, 0};
        case KEY_BACKSPACE: return {Key::Backspace, 0};
        case KEY_SPACE: return {Key::Space, 0};
        case KEY_UP: return {Key::Up, 0};
        case KEY_DOWN: return {Key::Down, 0};
        case KEY_LEFT: return {Key::Left, 0};
        case KEY_RIGHT: return {Key::Right, 0};
        default: break;
        }
        if (ev.code >= KEY_1 && ev.code <= KEY_9) return {Key::Char, static_cast<char>('1' + (ev.code - KEY_1))};
        if (ev.code == KEY_0) return {Key::Char, '0'};
        if (ev.code >= KEY_Q && ev.code <= KEY_M && letters[ev.code - KEY_Q])
            return {Key::Char, letters[ev.code - KEY_Q]};
        return {Key::Any, 0};
    }
    return {};
}

// ------------------------------------------------------------------ battery / network
int batteryPercent()
{
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator("/sys/class/power_supply", ec)) {
        std::ifstream f(e.path() / "capacity");
        int v;
        if (f >> v) return v;
    }
    return -1;
}

std::string localIp()
{
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return "";
    std::string best;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK)) continue;
        if (!(a->ifa_flags & IFF_UP)) continue;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf);
        const bool wifi = std::strncmp(a->ifa_name, "wl", 2) == 0;
        if (best.empty() || wifi) best = buf;
        if (wifi) break;
    }
    freeifaddrs(list);
    return best;
}

// ------------------------------------------------------------------ backlight
void Backlight::init()
{
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator("/sys/class/backlight", ec)) {
        path_ = (e.path() / "brightness").string();
        std::ifstream f(path_);
        f >> level_;
        break;
    }
}

void Backlight::set(bool on)
{
    on_ = on;
    if (path_.empty()) return;
    std::ofstream f(path_);
    // The launcher stores 0 when it uses its own default; fall back to a sensible level.
    f << (on ? (level_ > 0 ? level_ : 60) : 0);
}

void Backlight::restore()
{
    if (path_.empty() || level_ < 0) return;
    std::ofstream f(path_);
    f << level_;
}

// ------------------------------------------------------------------ settings
static std::string settingsPath()
{
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.config/lookout/settings";
}

std::string Settings::newToken()
{
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    unsigned char raw[16];
    std::ifstream r("/dev/urandom", std::ios::binary);
    r.read(reinterpret_cast<char*>(raw), sizeof raw);
    std::string t;
    for (unsigned char b : raw) t.push_back(alphabet[b % (sizeof alphabet - 1)]);
    return t;
}

void Settings::load()
{
    std::ifstream f(settingsPath());
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "token") token = v;
        else if (k == "quality") quality = std::atoi(v.c_str()) % 3;
        else if (k == "mic_gain") micGain = std::atoi(v.c_str()) % 3;
        else if (k == "mic") micEnabled = v == "1";
        else if (k == "rotate180") rotate180 = v == "1";
        else if (k == "port") port = std::atoi(v.c_str());
    }
    if (token.size() < 12) {
        token = newToken();
        save();
    }
    if (port < 1024 || port > 65535) port = 8080;
}

void Settings::save() const
{
    const std::string p = settingsPath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    const std::string tmp = p + ".tmp";
    {
        std::ofstream f(tmp);
        f << "token=" << token << "\nquality=" << quality << "\nmic_gain=" << micGain << "\nmic="
          << (micEnabled ? 1 : 0) << "\nrotate180=" << (rotate180 ? 1 : 0) << "\nport=" << port << "\n";
    }
    ::chmod(tmp.c_str(), 0600);  // the token grants viewing access
    std::filesystem::rename(tmp, p, ec);
}

// ------------------------------------------------------------------ child processes
bool Child::start(const std::vector<std::string>& args)
{
    stop();
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) != 0) return false;
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t parent = ::getpid();
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        // Child: die with the parent so the camera/mic never outlive the app.
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (::getppid() != parent) ::_exit(1);
        ::dup2(pipefd[1], 1);
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) ::dup2(devnull, 2);
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    ::close(pipefd[1]);
    pid_ = pid;
    fd_ = pipefd[0];
    return true;
}

bool Child::running()
{
    if (pid_ <= 0) return false;
    int status;
    if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return false;
    }
    return true;
}

void Child::stop()
{
    kill();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void Child::kill()
{
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        for (int i = 0; i < 20; ++i) {
            int status;
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                pid_ = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
    }
}

}  // namespace lk
