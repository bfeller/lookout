#include "net/server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "net/page.hpp"

namespace lk {
namespace {

constexpr int kMaxClients = 12;
constexpr int kMaxVideo = 4;
constexpr int kMaxAudio = 4;

bool sendAll(int fd, const char* p, size_t n)
{
    while (n > 0) {
        const ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}
bool sendStr(int fd, const std::string& s) { return sendAll(fd, s.data(), s.size()); }

std::string urlDecode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out.push_back(static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(s[i] == '+' ? ' ' : s[i]);
        }
    }
    return out;
}

std::string param(const std::string& query, const std::string& key)
{
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == key) return urlDecode(part.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

// Constant-time comparison so the token can't be guessed byte by byte from response timing.
bool tokenMatches(const std::string& a, const std::string& b)
{
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

const char* kCommonHeaders =
    "Cache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\n"
    "Connection: close\r\n";

void respond(int fd, const char* status, const char* type, const std::string& body)
{
    std::string h = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type +
                    "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + kCommonHeaders + "\r\n";
    sendStr(fd, h);
    sendStr(fd, body);
}

struct Counter {
    std::atomic<int>& c;
    explicit Counter(std::atomic<int>& x) : c(x) { ++c; }
    ~Counter() { --c; }
};

}  // namespace

Server::~Server()
{
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
    }
    if (acceptThread_.joinable()) acceptThread_.detach();
}

bool Server::start(int port, std::string& error)
{
    for (int p = port; p < port + 10; ++p) {
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<uint16_t>(p));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 && ::listen(fd, 16) == 0) {
            listenFd_ = fd;
            port_ = p;
            acceptThread_ = std::thread(&Server::acceptLoop, this);
            return true;
        }
        ::close(fd);
    }
    error = "couldn't open a network port (" + std::to_string(port) + "-" + std::to_string(port + 9) + ")";
    return false;
}

void Server::acceptLoop()
{
    for (;;) {
        const int fd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (clients_.load() >= kMaxClients) {
            ::close(fd);
            continue;
        }
        timeval tv{5, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        std::thread([this, fd] {
            Counter c(clients_);
            handle(fd);
            ::close(fd);
        }).detach();
    }
}

void Server::handle(int fd)
{
    // Read the request head (we only need the request line).
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return;
        req.append(buf, static_cast<size_t>(n));
    }
    const size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return;
    const std::string method = req.substr(0, sp1), target = req.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    const std::string path = target.substr(0, q);
    const std::string query = q == std::string::npos ? "" : target.substr(q + 1);
    lastRequest_ = nowMs();

    if (method != "GET") return respond(fd, "405 Method Not Allowed", "text/plain", "GET only\n");
    if (path == "/favicon.ico") return respond(fd, "404 Not Found", "text/plain", "");
    if (!tokenMatches(param(query, "k"), hooks_.token()))
        return respond(fd, "403 Forbidden", "text/html; charset=utf-8", kDeniedPage);

    if (path == "/") return respond(fd, "200 OK", "text/html; charset=utf-8", kViewerPage);
    if (path == "/video") return streamVideo(fd);
    if (path == "/audio") return streamAudio(fd);
    if (path == "/status") return respond(fd, "200 OK", "application/json", hooks_.statusJson());
    if (path == "/snapshot.jpg") {
        Counter v(viewers_);  // keeps the camera running while we wait for a frame
        uint64_t seq = 0;
        Frame f = video_.latest(&seq);
        if (!f) f = video_.waitNewer(0, seq, 6000);
        if (!f) return respond(fd, "503 Service Unavailable", "text/plain", "camera not ready\n");
        return respond(fd, "200 OK", "image/jpeg", *f);
    }
    if (path == "/set") {
        for (const char* key : {"quality", "rotate", "gain"}) {
            const std::string v = param(query, key);
            if (!v.empty()) {
                const bool ok = hooks_.set(key, std::atoi(v.c_str()));
                return respond(fd, ok ? "200 OK" : "400 Bad Request", "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            }
        }
        return respond(fd, "400 Bad Request", "text/plain", "unknown setting\n");
    }
    respond(fd, "404 Not Found", "text/plain", "not found\n");
}

void Server::streamVideo(int fd)
{
    if (viewers_.load() >= kMaxVideo) return respond(fd, "503 Service Unavailable", "text/plain", "too many viewers\n");
    Counter v(viewers_);
    std::string h = std::string("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=lkframe\r\n") +
                    kCommonHeaders + "\r\n";
    if (!sendStr(fd, h)) return;
    uint64_t seq = 0;
    int64_t lastData = nowMs();
    const int gen = generation_.load();
    for (;;) {
        if (generation_.load() != gen) return;
        Frame f = video_.waitNewer(seq, seq, 1000);
        if (!f) {
            // Camera starting or restarting; give up only after a long silence.
            if (nowMs() - lastData > 30000) return;
            continue;
        }
        lastData = nowMs();
        const std::string part = "--lkframe\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                                 std::to_string(f->size()) + "\r\n\r\n";
        if (!sendStr(fd, part) || !sendStr(fd, *f) || !sendStr(fd, "\r\n")) return;
    }
}

void Server::streamAudio(int fd)
{
    if (listeners_.load() >= kMaxAudio) return respond(fd, "503 Service Unavailable", "text/plain", "too many listeners\n");
    Counter l(listeners_);
    std::string h = std::string("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n") + kCommonHeaders + "\r\n";
    if (!sendStr(fd, h)) return;
    uint64_t cursor = 0;
    int64_t lastData = nowMs();
    const int gen = generation_.load();
    for (;;) {
        if (generation_.load() != gen) return;
        std::string out;
        if (!audio_.read(cursor, out, 1000)) {
            if (nowMs() - lastData > 30000) return;  // mic disabled or not starting
            continue;
        }
        lastData = nowMs();
        if (!sendStr(fd, out)) return;
    }
}

}  // namespace lk
