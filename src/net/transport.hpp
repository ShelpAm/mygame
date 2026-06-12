#pragma once

#include "net/net-packet.hpp"

#include <boost/asio.hpp>
#include <cstdint>
#include <vector>

namespace asio = boost::asio;

using asio::as_tuple;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

struct TransportMessage {
    NetPacket::Type type;
    std::vector<uint8_t> payload;
};

// Abstract bidirectional pipe between two endpoints.
// Server and Client both speak through this without knowing
// whether the other side is in-process (LocalTransportEndpoint)
// or across the network (NetworkTransport wrapping sockets).
class ITransport {
  public:
    virtual ~ITransport() = default;

    static asio::io_context &io();
    static void shutdown();

    // Push a message to the peer. Non-blocking.
    virtual awaitable<void> write(TransportMessage msg) = 0;

    // Drain the inbound queue, invoking the callback for each message.
    // Called once per frame from the game loop.
    virtual awaitable<TransportMessage> read() = 0;

    // Whether the transport is still connected to its peer.
    virtual bool is_connected() const = 0;
};
