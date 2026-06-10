#pragma once

#include "net/net-packet.hpp"

#include <cstdint>
#include <functional>
#include <vector>

// Wire format: type (4 bytes) + payload_size (4 bytes) + payload.
// type values match NetPacket::Type — see net/net-packet.hpp.
//
// state_full     = 1   server → client: full entity snapshot (22 bytes per
// entity) entity_update  = 2   client → server: player position + HP chat = 3
// bidirectional: chat text combat_event   = 5   server → client: damage dealt
// (13 bytes) recruit_soldier= 6   client → server: soldier spawn request
// spawn_enemy_wave=7   client → server: enemy wave request
// player_input   = 8   client → server: normalized move direction (mx, my as
// floats)

struct TransportMessage {
    NetPacket::Type type;
    std::vector<uint8_t> payload;
};

// Abstract bidirectional pipe between two endpoints.
// Server and Client both speak through this without knowing
// whether the other side is in-process (LocalTransportEndpoint)
// or across the network (NetworkTransport wrapping sockets).
//
// Lifecycle per session:
//   1. attach — set_callback(cb) registers the message handler
//   2. run  — update() drains inbound queue each frame, calling cb
//   3. stop — transport is destroyed; endpoint detaches
//
// Thread safety: send() may be called from any thread.
// update() must be called from the game loop thread.
class ITransport {
  public:
    using Callback = std::function<void(TransportMessage const &)>;

    virtual ~ITransport() = default;

    // Push a message to the peer. Non-blocking.
    virtual void send(TransportMessage msg) = 0;

    // Register the handler for incoming messages.
    // Only one callback at a time; replaces any previous one.
    virtual void set_callback(Callback cb) = 0;

    // Drain the inbound queue, invoking the callback for each message.
    // Called once per frame from the game loop.
    virtual void update() = 0;

    // Whether the transport is still connected to its peer.
    virtual bool is_connected() const = 0;
};
