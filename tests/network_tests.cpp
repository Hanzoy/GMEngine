#include "network.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace gm::sdk;
namespace {
const auto &net = gm::networkService();
Error error{};
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(std::string(message) + ": " + error.message);
}
struct Session {
    void *p;
    ~Session() {
        net.close(p);
    }
};
void pump(std::initializer_list<void *> sessions) {
    for (auto s : sessions)
        check(net.pump(s, &error), "pump");
}
template <class F> void until(F fn, std::initializer_list<void *> sessions) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!fn()) {
        check(std::chrono::steady_clock::now() < end, "network deadline");
        pump(sessions);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
NetEvent take(void *session, NetEventKind kind) {
    NetEvent e;
    check(net.poll(session, &e) && e.kind == kind, "unexpected event");
    return e;
}
void malformedWire() {
    NetConfig config;
    config.game = id("gm.network.raw");
    config.address = "127.0.0.1";
    config.port = 0;
    config.advertise = 0;
    Session host{net.host(&config, &error)};
    check(host.p, "raw test host");
    auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    check(socket != INVALID_SOCKET, "raw socket");
    struct SocketGuard {
        SOCKET value;
        ~SocketGuard() {
            closesocket(value);
        }
    } guard{socket};
#else
    check(socket >= 0, "raw socket");
    struct SocketGuard {
        int value;
        ~SocketGuard() {
            ::close(value);
        }
    } guard{socket};
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(net.stats(host.p).port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    check(::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "raw connect");
    const uint8_t header[] = {0xff, 0xff, 0xff, 0x7f};
    check(::send(socket, reinterpret_cast<const char *>(header), 2, 0) == 2, "partial header send");
    until([&] { return net.stats(host.p).receivedBytes == 2; }, {host.p});
    NetEvent event;
    check(!net.poll(host.p, &event), "Partial header must not become an event");
    check(::send(socket, reinterpret_cast<const char *>(header + 2), 2, 0) == 2, "oversized header send");
    until([&] { return net.poll(host.p, &event); }, {host.p});
    check(event.kind == NetEventKind::Disconnected && event.reason == NetReason::Protocol,
          "Reject oversized wire length before allocation");
    check(net.stats(host.p).connectedPeers == 0 && net.stats(host.p).receivedMessages == 0,
          "Malformed peer never reaches application");
}
} // namespace
int main() {
    try {
        NetConfig config;
        config.game = id("gm.network.contract");
        config.port = 0;
        config.address = "127.0.0.1";
        Session host{net.host(&config, &error)};
        check(host.p, "host");
        config.port = net.stats(host.p).port;
        Session client{net.connect(&config, &error)}, other{net.connect(&config, &error)};
        check(client.p && other.p, "connect");
        until(
            [&] { return net.stats(client.p).connectedPeers == 1 && net.stats(other.p).connectedPeers == 1; },
            {host.p, client.p, other.p});
        auto a = take(host.p, NetEventKind::Connected).peer, b = take(host.p, NetEventKind::Connected).peer;
        check(a != b && a >= 2 && b >= 2, "peer identities");
        take(client.p, NetEventKind::Connected);
        take(other.p, NetEventKind::Connected);
        check(net.stats(client.p).localPeer != net.stats(other.p).localPeer, "unique assigned peer IDs");
        const uint8_t payload[] = {0, 1, 255, 0, 42};
        check(net.send(client.p, 1, 7, payload, sizeof(payload), &error), "client send");
        NetEvent received;
        until([&] { return net.poll(host.p, &received); }, {host.p, client.p});
        check(received.kind == NetEventKind::Message && received.peer == net.stats(client.p).localPeer &&
                  received.channel == 7 && received.size == sizeof(payload) &&
                  !std::memcmp(received.data, payload, sizeof(payload)),
              "message boundary/identity/payload");
        // Simulated authoritative state: host validates commands, then sends identical state to peers.
        check(net.send(host.p, 0, 8, payload, sizeof(payload), &error), "broadcast");
        until([&] { return net.poll(client.p, &received); }, {host.p, client.p, other.p});
        check(received.channel == 8 && received.peer == 1 && received.size == sizeof(payload),
              "host snapshot");
        until([&] { return net.poll(other.p, &received); }, {host.p, client.p, other.p});
        check(received.channel == 8 && !std::memcmp(received.data, payload, sizeof(payload)),
              "same broadcast state");
        check(!net.send(client.p, 0, 0, payload, sizeof(payload), &error), "client must not broadcast");
        check(!net.send(client.p, 1, 256, payload, sizeof(payload), &error), "channel bounds");
        check(!net.send(client.p, 1, 0, payload, NetworkService::MaxMessage + 1, &error), "message bounds");
        std::vector<uint8_t> large(NetworkService::MaxMessage, 0x5a);
        for (uint32_t i = 0; i < 15; ++i)
            check(net.send(client.p, 1, i, large.data(), uint32_t(large.size()), &error),
                  "queue large message");
        check(!net.send(client.p, 1, 16, large.data(), uint32_t(large.size()), &error), "bounded send queue");
        uint32_t count = 0;
        until(
            [&] {
                while (net.poll(host.p, &received)) {
                    check(received.kind == NetEventKind::Message && received.channel == count &&
                              received.size == large.size() &&
                              !std::memcmp(received.data, large.data(), large.size()),
                          "ordered complete large messages");
                    ++count;
                }
                return count == 15;
            },
            {host.p, client.p});
        Session scan{net.discover(&config, &error)};
        check(scan.p, "discovery");
        until([&] { return net.poll(scan.p, &received); }, {host.p, scan.p});
        check(received.kind == NetEventKind::Room && received.port == config.port &&
                  !std::strcmp(received.address, "127.0.0.1"),
              "LAN discovery reply");
        // Different content/protocol must be rejected before application messages are delivered.
        auto incompatible = config;
        ++incompatible.protocol;
        Session bad{net.connect(&incompatible, &error)};
        check(bad.p, "incompatible connect begins");
        until([&] { return net.poll(bad.p, &received); }, {host.p, bad.p});
        check(received.kind == NetEventKind::Disconnected, "protocol rejection");
        bool wrongThreadAccepted = true;
        std::thread worker([&] {
            Error e;
            wrongThreadAccepted = net.pump(client.p, &e);
        });
        worker.join();
        check(!wrongThreadAccepted, "owner-thread contract");
        auto clientPeer = net.stats(client.p).localPeer;
        check(net.disconnect(host.p, clientPeer, &error), "disconnect");
        until([&] { return net.poll(client.p, &received); }, {host.p, client.p});
        check(received.kind == NetEventKind::Disconnected && net.stats(client.p).connectedPeers == 0,
              "disconnect notification");
        Session reconnect{net.connect(&config, &error)};
        check(reconnect.p, "reconnect");
        until([&] { return net.stats(reconnect.p).connectedPeers == 1; }, {host.p, reconnect.p});
        check(net.stats(reconnect.p).localPeer != clientPeer, "connection identities are not reused");
        check(net.stats(host.p).receivedMessages == 16, "network diagnostics");
        malformedWire();
        std::cout << "LAN handshake, discovery, two clients, authoritative broadcast, framing, ordering, "
                     "backpressure, version rejection, malformed wire rejection, disconnect, reconnect and "
                     "thread contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
