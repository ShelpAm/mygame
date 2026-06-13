#pragma once

#include "net/net-packet.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <vector>

namespace asio = boost::asio;

using asio::as_tuple;
using asio::awaitable;
using asio::co_spawn;
using asio::deferred;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

// using default_token = asio::use_awaitable_t<>;
using default_token = asio::deferred_t;

template <typename T>
using deferred_concurrent_channel =
    default_token::as_default_on_t<asio::experimental::concurrent_channel<T>>;

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
    static void set_io(asio::io_context *io)
    {
        s_io_ = io;
    }
    static asio::io_context &io()
    {
        if (!s_io_)
            throw std::runtime_error(
                "ITransport: access io_context before set");
        return *s_io_;
    }
    static void spawn(auto awaitable)
    {
        // co_spawn(io(), std::move(awaitable), detached);
        co_spawn(io(), std::move(awaitable), [](std::exception_ptr ep) {
            if (ep)
                try {
                    std::rethrow_exception(ep);
                }
                catch (std::exception const &e) {
                    spdlog::error("ITransport: unhandled exception in spawned "
                                  "coroutine: {}",
                                  e.what());
                }
                catch (...) {
                    spdlog::error(
                        "ITransport: unhandled unknown exception in spawned "
                        "coroutine");
                }
        });
    }

  public:
    virtual ~ITransport() = default;

    // Push a message to the peer. Non-blocking.
    virtual awaitable<void> write(TransportMessage msg) = 0;

    virtual awaitable<TransportMessage> read() = 0;

    // Whether the transport is still connected to its peer.
    virtual bool is_connected() const = 0;

    virtual void disconnect() = 0;

  private:
    static inline asio::io_context *s_io_ = nullptr;
};
