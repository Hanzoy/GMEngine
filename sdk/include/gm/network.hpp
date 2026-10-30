#pragma once
#include "extensions.hpp"

namespace gm::sdk {
// Poll/pump/send and close belong to the creating thread (normally the System owner).
// Peer 1 is the host. Clients send to 1; hosts may send to 0 to broadcast.
// Peer IDs identify connections, never ECS entities or authenticated player accounts.
enum class NetTransport : uint32_t { Lan = 1 };
enum class NetEventKind : uint32_t { Connected, Disconnected, Message, Room };
enum class NetReason : uint32_t { None, Closed, Transport, Protocol, Timeout, Capacity };
struct NetConfig {
    uint32_t structSize = sizeof(NetConfig);
    NetTransport transport = NetTransport::Lan;
    uint64_t game = 0, protocol = 1;
    uint16_t port = 27030;
    uint16_t maxPeers = 7;
    // Numeric IPv4 only. Host defaults to all interfaces; connect requires an address.
    const char *address = nullptr;
    const char *name = "GM Engine room";
    uint32_t advertise = 1;
};
struct NetEvent {
    NetEventKind kind{};
    uint32_t peer = 0, channel = 0, size = 0;
    // Borrowed until the next poll/close. Copy data if retained beyond that boundary.
    const void *data = nullptr;
    NetReason reason{};
    char address[48]{}, name[96]{};
    uint16_t port = 0, peers = 0;
};
struct NetStats {
    uint32_t localPeer = 0, connectedPeers = 0, pendingPeers = 0, queuedBytes = 0;
    uint64_t sentBytes = 0, receivedBytes = 0, sentMessages = 0, receivedMessages = 0;
    uint16_t port = 0;
};
struct NetworkService {
    static constexpr TypeId Id = id("gm.Network");
    static constexpr uint32_t Version = 1;
    static constexpr uint32_t MaxMessage = 64 * 1024;
    uint32_t structSize, version;
    void *(*host)(const NetConfig *, Error *);
    void *(*connect)(const NetConfig *, Error *);
    // UDP scan sent once. Recreate to refresh; address defaults to 255.255.255.255.
    void *(*discover)(const NetConfig *, Error *);
    void (*close)(void *);
    // Nonblocking, bounded work. Pump at least once per frame; keep pumping when paused.
    bool (*pump)(void *, Error *);
    bool (*poll)(void *, NetEvent *);
    // Reliable, ordered messages. No implicit forwarding or ECS memory replication.
    // false means nothing was queued (including broadcasts): callers must handle backpressure.
    bool (*send)(void *, uint32_t peer, uint32_t channel, const void *, uint32_t, Error *);
    bool (*disconnect)(void *, uint32_t peer, Error *);
    NetStats (*stats)(void *);
};
} // namespace gm::sdk
