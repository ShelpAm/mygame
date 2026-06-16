#pragma once

#include "net/local-session.hpp"
#include "net/net-packet.hpp"
#include "net/network-session.hpp"
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace asio = boost::asio;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;

struct NetworkFixture {
    asio::io_context io;
    std::jthread io_thread;
    decltype(asio::make_work_guard(io)) work = asio::make_work_guard(io);

    NetworkFixture()
    {
        Session::set_io(&io);
        io_thread = std::jthread([this]() { io.run(); });
    }
    ~NetworkFixture()
    {
        work.reset();
        io.stop();
        if (io_thread.joinable())
            io_thread.join();
    }
};

BOOST_GLOBAL_FIXTURE(NetworkFixture);

template <typename T> static T run_sync(asio::awaitable<T> a)
{
    std::promise<T> promise;
    auto future = promise.get_future();
    asio::co_spawn(
        Session::io(),
        [&]() -> asio::awaitable<void> {
            try {
                if constexpr (std::is_void_v<T>) {
                    co_await std::move(a);
                    promise.set_value();
                }
                else {
                    promise.set_value(co_await std::move(a));
                }
            }
            catch (...) {
                promise.set_exception(std::current_exception());
            }
        },
        asio::detached);
    return future.get();
}

// Minimal concrete Session for testing the interface contract
struct MockSession : Session {
    awaitable<void> write(TransportMessage) override { co_return; }
    awaitable<TransportMessage> read() override
    {
        co_return TransportMessage{};
    }
    bool is_open() const override { return true; }
    void close() override {}
    std::string remote_info() const override { return "mock"; }
};
