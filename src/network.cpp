#include "network.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace gm {
namespace {
using namespace sdk;
using Clock = std::chrono::steady_clock;
constexpr size_t QueueLimit = 1024 * 1024, EventLimit = 512, IoBudget = 256 * 1024;
constexpr uint32_t Hello = 1, Welcome = 2, Data = 3, Ping = 4;
#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket Invalid = INVALID_SOCKET;
void closeSocket(Socket s) {
    if (s != Invalid)
        closesocket(s);
}
int socketError() {
    return WSAGetLastError();
}
bool pending(int e) {
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void initialize() {
    static struct Winsock {
        Winsock() {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data))
                throw std::runtime_error("Cannot initialize Winsock");
        }
        ~Winsock() {
            WSACleanup();
        }
    } winsock;
}
void nonblocking(Socket s) {
    u_long on = 1;
    if (ioctlsocket(s, FIONBIO, &on))
        throw std::runtime_error("Cannot set nonblocking socket");
}
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket Invalid = -1;
void closeSocket(Socket s) {
    if (s != Invalid)
        ::close(s);
}
int socketError() {
    return errno;
}
bool pending(int e) {
    return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS;
}
void initialize() {}
void nonblocking(Socket s) {
    if (fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK) < 0)
        throw std::runtime_error("Cannot set nonblocking socket");
}
#endif
struct Handle {
    Socket value = Invalid;
    Handle() = default;
    explicit Handle(Socket s) : value(s) {}
    Handle(const Handle &) = delete;
    Handle(Handle &&o) noexcept : value(std::exchange(o.value, Invalid)) {}
    Handle &operator=(Handle &&o) noexcept {
        if (this != &o) {
            closeSocket(value);
            value = std::exchange(o.value, Invalid);
        }
        return *this;
    }
    ~Handle() {
        closeSocket(value);
    }
};
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void error(Error *e, const char *s) {
    if (e) {
        *e = {};
        std::strncpy(e->message, s, sizeof(e->message) - 1);
    }
}
uint32_t word(const uint8_t *b) {
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
void put(std::vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
void put64(std::vector<uint8_t> &b, uint64_t v) {
    put(b, uint32_t(v));
    put(b, uint32_t(v >> 32));
}
uint64_t wide(const uint8_t *b) {
    return word(b) | uint64_t(word(b + 4)) << 32;
}
sockaddr_in address(const char *ip, uint16_t port, const char *fallback) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    check(inet_pton(AF_INET, ip ? ip : fallback, &a.sin_addr) == 1, "Expected numeric IPv4 address");
    return a;
}
Handle socket(int type) {
    Handle s(::socket(AF_INET, type, type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP));
    check(s.value != Invalid, "Cannot create network socket");
    nonblocking(s.value);
    int on = 1;
    if (type == SOCK_STREAM)
        setsockopt(s.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&on), sizeof(on));
#ifdef SO_NOSIGPIPE
    setsockopt(s.value, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    return s;
}
struct Peer {
    Handle socket;
    uint32_t id = 0;
    bool connecting = false, ready = false;
    std::vector<uint8_t> in, out;
    size_t sent = 0;
    Clock::time_point born = Clock::now(), received = born, ping = born;
};
struct Event {
    NetEvent value{};
    std::vector<uint8_t> bytes;
};
class Session {
  public:
    enum Mode { Host, Client, Scan };
    Session(const NetConfig &config, Mode mode) : config_(config), mode_(mode) {
        initialize();
        check(config.structSize >= sizeof(NetConfig) && config.transport == NetTransport::Lan &&
                  config.game && config.maxPeers >= 1 && config.maxPeers <= 31,
              "Invalid LAN config (game ID and 1..31 peers required)");
        if (config.name) {
            check(std::strlen(config.name) < 96, "Room name exceeds 95 bytes");
            name_ = config.name;
        }
        config_.name = nullptr;
        config_.address = nullptr;
        if (mode == Host) {
            listen_ = socket(SOCK_STREAM);
            auto a = address(config.address, config.port, "0.0.0.0");
            check(bind(listen_.value, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0,
                  "Cannot bind LAN port (already in use?)");
            check(listen(listen_.value, config.maxPeers) == 0, "Cannot listen on LAN port");
            SocketLength length = sizeof(a);
            check(getsockname(listen_.value, reinterpret_cast<sockaddr *>(&a), &length) == 0,
                  "Cannot read LAN port");
            stats_.port = ntohs(a.sin_port);
            stats_.localPeer = 1;
            if (config.advertise) {
                udp_ = socket(SOCK_DGRAM);
                a.sin_port = htons(stats_.port);
                check(bind(udp_.value, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0,
                      "Cannot bind discovery port");
            }
        } else if (mode == Client) {
            check(config.port && config.address, "Joining requires an IPv4 address and port");
            Peer p;
            p.socket = socket(SOCK_STREAM);
            p.id = 1;
            auto a = address(config.address, config.port, "");
            if (::connect(p.socket.value, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
                check(pending(socketError()), "Cannot start LAN connection");
                p.connecting = true;
            }
            queue(p, Hello, 0, handshake());
            peers_.push_back(std::move(p));
            stats_.port = config.port;
        } else {
            check(config.port != 0, "Discovery requires a port");
            udp_ = socket(SOCK_DGRAM);
            int on = 1;
            setsockopt(udp_.value, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&on), sizeof(on));
            auto a = address(config.address, config.port, "255.255.255.255");
            auto packet = handshake();
            put(packet, 0x51534d47);
            check(sendto(udp_.value, reinterpret_cast<const char *>(packet.data()), int(packet.size()), 0,
                         reinterpret_cast<sockaddr *>(&a), sizeof(a)) == int(packet.size()),
                  "Cannot send LAN discovery probe");
        }
    }
    void owner() const {
        check(thread_ == std::this_thread::get_id(), "Network session used from a different thread");
    }
    std::vector<uint8_t> handshake() const {
        std::vector<uint8_t> v;
        put(v, 0x314e4d47);
        put(v, 1);
        put64(v, config_.game);
        put64(v, config_.protocol);
        return v;
    }
    bool compatible(const uint8_t *p, size_t n) const {
        return n >= 24 && word(p) == 0x314e4d47 && word(p + 4) == 1 && wide(p + 8) == config_.game &&
               wide(p + 16) == config_.protocol;
    }
    static void queue(Peer &p, uint32_t kind, uint32_t channel, const std::vector<uint8_t> &data) {
        if (p.sent) {
            p.out.erase(p.out.begin(), p.out.begin() + p.sent);
            p.sent = 0;
        }
        put(p.out, uint32_t(data.size() + 8));
        put(p.out, kind);
        put(p.out, channel);
        p.out.insert(p.out.end(), data.begin(), data.end());
    }
    bool event(NetEventKind kind, uint32_t peer, NetReason reason = NetReason::None, uint32_t channel = 0,
               const uint8_t *data = nullptr, size_t size = 0) {
        if (events_.size() >= EventLimit || eventBytes_ + size > QueueLimit)
            return false;
        Event e;
        e.value.kind = kind;
        e.value.peer = peer;
        e.value.reason = reason;
        e.value.channel = channel;
        if (size)
            e.bytes.assign(data, data + size);
        eventBytes_ += size;
        events_.push_back(std::move(e));
        return true;
    }
    void drop(Peer &p, NetReason reason) {
        // Reserve control events separately from payload backpressure, bounded by maxPeers.
        Event e;
        e.value.kind = NetEventKind::Disconnected;
        e.value.peer = p.id;
        e.value.reason = reason;
        if (events_.size() < EventLimit + 32)
            events_.push_back(std::move(e));
        p.socket = Handle{};
    }
    void pump() {
        owner();
        if (mode_ == Host)
            for (int i = 0; i < 8; ++i) {
                Handle s(accept(listen_.value, nullptr, nullptr));
                if (s.value == Invalid) {
                    check(pending(socketError()), "LAN accept failed");
                    break;
                }
                if (peers_.size() >= config_.maxPeers)
                    continue;
                nonblocking(s.value);
                int on = 1;
                setsockopt(s.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&on),
                           sizeof(on));
                Peer p;
                p.socket = std::move(s);
                p.id = nextPeer_++;
                peers_.push_back(std::move(p));
            }
        discovery();
        const auto now = Clock::now();
        for (auto &p : peers_) {
            if (p.connecting) {
                fd_set writes, errors;
                FD_ZERO(&writes);
                FD_ZERO(&errors);
                FD_SET(p.socket.value, &writes);
                FD_SET(p.socket.value, &errors);
                timeval timeout{};
                int result = select(int(p.socket.value + 1), nullptr, &writes, &errors, &timeout);
                if (result < 0) {
                    drop(p, NetReason::Transport);
                    continue;
                }
                if (result > 0) {
                    int e = 0;
                    SocketLength n = sizeof(e);
                    if (getsockopt(p.socket.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&e), &n) !=
                            0 ||
                        e) {
                        drop(p, NetReason::Transport);
                        continue;
                    }
                    p.connecting = false;
                }
            }
            if ((!p.ready && now - p.born > std::chrono::seconds(10)) ||
                now - p.received > std::chrono::seconds(15)) {
                drop(p, NetReason::Timeout);
                continue;
            }
            if (p.connecting)
                continue;
            // Drain any previously buffered complete frames before admitting more bytes.
            bool good = decode(p);
            std::array<uint8_t, 16384> buffer{};
            for (size_t budget = 0;
                 good && budget < IoBudget && p.in.size() < NetworkService::MaxMessage + 12 &&
                 eventBytes_ < QueueLimit - NetworkService::MaxMessage && events_.size() < EventLimit;
                 ++budget) {
                int n = recv(p.socket.value, reinterpret_cast<char *>(buffer.data()),
                             int(std::min(buffer.size(), NetworkService::MaxMessage + 12 - p.in.size())), 0);
                if (n == 0) {
                    drop(p, NetReason::Closed);
                    good = false;
                    break;
                }
                if (n < 0) {
                    if (!pending(socketError())) {
                        drop(p, NetReason::Transport);
                        good = false;
                    }
                    break;
                }
                stats_.receivedBytes += n;
                budget += n;
                p.received = now;
                p.in.insert(p.in.end(), buffer.begin(), buffer.begin() + n);
                good = decode(p);
            }
            if (!good)
                continue;
            if (p.ready && now - p.ping > std::chrono::seconds(2) &&
                p.out.size() - p.sent < QueueLimit - 12) {
                queue(p, Ping, 0, {});
                p.ping = now;
            }
            size_t budget = IoBudget;
            while (p.sent < p.out.size() && budget) {
                const int count = int(std::min(budget, p.out.size() - p.sent));
#ifdef MSG_NOSIGNAL
                constexpr int flags = MSG_NOSIGNAL;
#else
                constexpr int flags = 0;
#endif
                int n = ::send(p.socket.value, reinterpret_cast<const char *>(p.out.data() + p.sent), count,
                               flags);
                if (n <= 0) {
                    if (n == 0 || !pending(socketError()))
                        drop(p, NetReason::Transport);
                    break;
                }
                p.sent += n;
                budget -= n;
                stats_.sentBytes += n;
            }
            if (p.sent == p.out.size()) {
                p.out.clear();
                p.sent = 0;
            }
        }
        std::erase_if(peers_, [](const Peer &p) { return p.socket.value == Invalid; });
    }
    bool decode(Peer &p) {
        size_t used = 0;
        while (p.in.size() - used >= 4) {
            const auto *b = p.in.data() + used;
            uint32_t length = word(b);
            if (length < 8 || length > NetworkService::MaxMessage + 8) {
                drop(p, NetReason::Protocol);
                return false;
            }
            if (p.in.size() - used < length + 4)
                break;
            auto kind = word(b + 4), channel = word(b + 8);
            const auto *payload = b + 12;
            size_t size = length - 8;
            if (!p.ready) {
                if (channel || !compatible(payload, size) ||
                    (mode_ == Host ? (kind != Hello || size != 24)
                                   : (kind != Welcome || size != 28 || word(payload + 24) < 2))) {
                    drop(p, NetReason::Protocol);
                    return false;
                }
                if (!event(NetEventKind::Connected, p.id))
                    break;
                p.ready = true;
                if (mode_ == Host) {
                    auto h = handshake();
                    put(h, p.id);
                    queue(p, Welcome, 0, h);
                } else
                    stats_.localPeer = word(payload + 24);
            } else if (kind == Data) {
                if (channel > 255) {
                    drop(p, NetReason::Protocol);
                    return false;
                }
                if (!event(NetEventKind::Message, p.id, NetReason::None, channel, payload, size))
                    break;
                ++stats_.receivedMessages;
            } else if (kind != Ping || size || channel) {
                drop(p, NetReason::Protocol);
                return false;
            }
            used += length + 4;
        }
        if (used)
            p.in.erase(p.in.begin(), p.in.begin() + used);
        return true;
    }
    void discovery() {
        if (udp_.value == Invalid)
            return;
        for (int i = 0; i < 16; ++i) {
            std::array<uint8_t, 256> bytes{};
            sockaddr_in from{};
            SocketLength length = sizeof(from);
            int n = recvfrom(udp_.value, reinterpret_cast<char *>(bytes.data()), int(bytes.size()), 0,
                             reinterpret_cast<sockaddr *>(&from), &length);
            if (n < 0) {
                if (pending(socketError()))
                    break;
                return;
            }
            if (!compatible(bytes.data(), n))
                continue;
            if (mode_ == Host && n == 28 && word(bytes.data() + 24) == 0x51534d47) {
                auto response = handshake();
                put(response, 0x52534d47);
                put(response, stats_.port);
                put(response, uint32_t(peers_.size()));
                response.insert(response.end(), name_.begin(), name_.end());
                sendto(udp_.value, reinterpret_cast<const char *>(response.data()), int(response.size()), 0,
                       reinterpret_cast<sockaddr *>(&from), length);
            } else if (mode_ == Scan && n >= 36 && n < 132 && word(bytes.data() + 24) == 0x52534d47 &&
                       word(bytes.data() + 28) > 0 && word(bytes.data() + 28) <= 65535 &&
                       events_.size() < 64) {
                Event e;
                e.value.kind = NetEventKind::Room;
                e.value.port = uint16_t(word(bytes.data() + 28));
                e.value.peers = uint16_t(word(bytes.data() + 32));
                inet_ntop(AF_INET, &from.sin_addr, e.value.address, sizeof(e.value.address));
                std::memcpy(e.value.name, bytes.data() + 36, n - 36);
                events_.push_back(std::move(e));
            }
        }
    }
    bool poll(NetEvent &result) {
        owner();
        current_ = {};
        result = {};
        if (events_.empty())
            return false;
        current_ = std::move(events_.front());
        events_.pop_front();
        eventBytes_ -= current_.bytes.size();
        result = current_.value;
        result.data = current_.bytes.empty() ? nullptr : current_.bytes.data();
        result.size = uint32_t(current_.bytes.size());
        return true;
    }
    void send(uint32_t peer, uint32_t channel, const void *data, uint32_t size) {
        owner();
        check(mode_ != Scan && channel <= 255 && size <= NetworkService::MaxMessage && (data || !size),
              "Invalid LAN message");
        check(mode_ == Host || peer == 1, "Clients send only to the host (peer 1)");
        size_t targets = 0;
        for (auto &p : peers_)
            if ((peer == 0 || p.id == peer) && p.ready) {
                ++targets;
                check(p.out.size() - p.sent + size + 12 <= QueueLimit,
                      "LAN send queue full; retry after pumping");
            }
        check(targets > 0, "Peer is not connected");
        std::vector<uint8_t> bytes;
        if (size)
            bytes.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
        for (auto &p : peers_)
            if ((peer == 0 || p.id == peer) && p.ready) {
                queue(p, Data, channel, bytes);
                ++stats_.sentMessages;
            }
    }
    void disconnect(uint32_t peer) {
        owner();
        for (auto &p : peers_)
            if (p.id == peer) {
                drop(p, NetReason::Closed);
                std::erase_if(peers_, [](const Peer &v) { return v.socket.value == Invalid; });
                return;
            }
        throw std::runtime_error("Unknown peer");
    }
    NetStats stats() const {
        owner();
        auto s = stats_;
        for (const auto &p : peers_) {
            if (p.ready)
                ++s.connectedPeers;
            else
                ++s.pendingPeers;
            s.queuedBytes += uint32_t(p.out.size() - p.sent);
        }
        return s;
    }

  private:
    NetConfig config_;
    Mode mode_;
    std::string name_;
    std::thread::id thread_ = std::this_thread::get_id();
    Handle listen_, udp_;
    std::vector<Peer> peers_;
    std::deque<Event> events_;
    Event current_;
    size_t eventBytes_ = 0;
    uint32_t nextPeer_ = 2;
    NetStats stats_{};
};
Session &session(void *s) {
    check(s != nullptr, "Missing LAN session");
    return *static_cast<Session *>(s);
}
template <class F> bool guard(Error *e, F &&f) {
    error(e, "");
    try {
        f();
        return true;
    } catch (const std::exception &x) {
        error(e, x.what());
        return false;
    } catch (...) {
        error(e, "Network operation failed");
        return false;
    }
}
void *open(const NetConfig *c, Session::Mode mode, Error *e) {
    void *result = nullptr;
    guard(e, [&] {
        check(c && c->structSize >= sizeof(NetConfig), "Missing or incompatible network config");
        result = new Session(*c, mode);
    });
    return result;
}
} // namespace
const sdk::NetworkService &networkService() {
    static const sdk::NetworkService service{
        sizeof(sdk::NetworkService),
        1,
        [](const sdk::NetConfig *c, sdk::Error *e) { return open(c, Session::Host, e); },
        [](const sdk::NetConfig *c, sdk::Error *e) { return open(c, Session::Client, e); },
        [](const sdk::NetConfig *c, sdk::Error *e) { return open(c, Session::Scan, e); },
        [](void *s) { delete static_cast<Session *>(s); },
        [](void *s, sdk::Error *e) { return guard(e, [&] { session(s).pump(); }); },
        [](void *s, sdk::NetEvent *e) {
            try {
                return e && session(s).poll(*e);
            } catch (...) {
                return false;
            }
        },
        [](void *s, uint32_t p, uint32_t ch, const void *b, uint32_t n, sdk::Error *e) {
            return guard(e, [&] { session(s).send(p, ch, b, n); });
        },
        [](void *s, uint32_t p, sdk::Error *e) { return guard(e, [&] { session(s).disconnect(p); }); },
        [](void *s) {
            try {
                return session(s).stats();
            } catch (...) {
                return sdk::NetStats{};
            }
        }};
    return service;
}
} // namespace gm
