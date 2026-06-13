#include "net/local-transport.hpp"
#include "net/net-packet.hpp"
#include "net/network-transport.hpp"
#include <boost/asio.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <future>
#include <thread>

namespace asio = boost::asio;

struct NetworkFixture {
    asio::io_context io;
    std::jthread io_thread;
    decltype(asio::make_work_guard(io)) work = asio::make_work_guard(io);

    NetworkFixture()
    {
        ITransport::set_io(&io);
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

// Helper: run a coroutine on the global IO thread, block until done.
template <typename T> static T run_sync(asio::awaitable<T> a)
{
    std::promise<T> promise;
    auto future = promise.get_future();
    asio::co_spawn(
        ITransport::io(),
        [&]() -> asio::awaitable<void> {
            if constexpr (std::is_void_v<T>)
                co_await std::move(a), promise.set_value();
            else
                promise.set_value(co_await std::move(a));
        },
        asio::detached);
    return future.get();
}

BOOST_AUTO_TEST_SUITE(network_tests)

// -- Packet serialization --
BOOST_AUTO_TEST_CASE(write_read_int_roundtrip)
{
    std::vector<uint8_t> buf;
    write_bytes(buf, uint32_t{42});
    BOOST_TEST(buf.size() == 4);
    BOOST_TEST(read_bytes<uint32_t>(buf, 0) == 42);
}

BOOST_AUTO_TEST_CASE(write_read_mixed_types)
{
    std::vector<uint8_t> buf;
    write_bytes(buf, uint16_t{100});
    write_bytes(buf, uint32_t{99999});
    BOOST_TEST(read_bytes<uint16_t>(buf, 0) == 100);
    BOOST_TEST(read_bytes<uint32_t>(buf, 2) == 99999);
}

BOOST_AUTO_TEST_CASE(serialize_packet_roundtrip)
{
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    auto data = serialize_packet({NetPacket::chat, payload});
    // 4 bytes type + 4 bytes size + payload
    BOOST_TEST(data.size() == 8 + payload.size());
    // Verify header
    auto type = read_bytes<uint32_t>(data, 0);
    auto size = read_bytes<uint32_t>(data, 4);
    BOOST_TEST(type == static_cast<uint32_t>(NetPacket::chat));
    BOOST_TEST(size == payload.size());
    // Verify payload
    BOOST_TEST(data[8] == 0x01);
    BOOST_TEST(data[9] == 0x02);
    BOOST_TEST(data[10] == 0x03);
}

BOOST_AUTO_TEST_CASE(make_and_parse_entity_update)
{
    auto data = make_entity_update(7, 1.5f, -2.0f, 10, 20, true);
    auto header_type = read_bytes<uint32_t>(data, 0);
    auto header_size = read_bytes<uint32_t>(data, 4);
    BOOST_TEST(header_type == static_cast<uint32_t>(NetPacket::entity_update));

    std::vector<uint8_t> payload(data.begin() + 8, data.end());
    auto u = parse_entity_update(payload);
    BOOST_TEST(u.id == 7);
    BOOST_TEST(u.x == 1.5f);
    BOOST_TEST(u.y == -2.0f);
    BOOST_TEST(u.hp == 10);
    BOOST_TEST(u.max_hp == 20);
    BOOST_TEST(u.alive);
}

BOOST_AUTO_TEST_CASE(make_and_parse_combat_event)
{
    auto data = make_combat_event(1, 3, 25, true);
    std::vector<uint8_t> payload(data.begin() + 8, data.end());
    auto ev = parse_combat_event(payload);
    BOOST_TEST(ev.attacker_id == 1);
    BOOST_TEST(ev.defender_id == 3);
    BOOST_TEST(ev.damage == 25);
    BOOST_TEST(ev.killed);
}

BOOST_AUTO_TEST_CASE(make_chat_preserves_text)
{
    auto data = make_chat("hello world");
    std::vector<uint8_t> payload(data.begin() + 8, data.end());
    std::string text(payload.begin(), payload.end());
    BOOST_TEST(text == "hello world");
}

// -- Local transport --
BOOST_AUTO_TEST_CASE(local_transport_send_receive)
{
    auto [a, b] = create_transport_pair();
    std::vector<uint8_t> pl = {'h', 'i'};

    run_sync([&]() -> asio::awaitable<void> {
        co_await b->write({NetPacket::chat, pl});
        auto msg = co_await a->read();
        BOOST_TEST(msg.type == NetPacket::chat);
        BOOST_TEST(msg.payload == pl);
    }());
}

BOOST_AUTO_TEST_CASE(local_transport_bidirectional)
{
    auto [a, b] = create_transport_pair();

    run_sync([&]() -> asio::awaitable<void> {
        co_await b->write({NetPacket::chat, {}});
        auto m1 = co_await a->read();
        BOOST_TEST(m1.type == NetPacket::chat);

        co_await a->write({NetPacket::join, {}});
        auto m2 = co_await b->read();
        BOOST_TEST(m2.type == NetPacket::join);
    }());
}

BOOST_AUTO_TEST_CASE(local_transport_multiple_messages)
{
    auto [a, b] = create_transport_pair();

    run_sync([&]() -> asio::awaitable<void> {
        for (int i = 0; i < 5; ++i)
            co_await b->write({NetPacket::chat, {}});
        for (int i = 0; i < 5; ++i) {
            auto msg = co_await a->read();
            BOOST_TEST(msg.type == NetPacket::chat);
        }
    }());
}

BOOST_AUTO_TEST_CASE(local_transport_is_connected)
{
    auto [a, b] = create_transport_pair();
    BOOST_TEST(a->is_connected());
    BOOST_TEST(b->is_connected());
}

// -- NetworkTransport lifecycle --
BOOST_AUTO_TEST_CASE(network_transport_not_connected_initially)
{
    auto peer = std::make_unique<NetworkTransport>();
    BOOST_TEST(!peer->is_connected());
}

BOOST_AUTO_TEST_CASE(network_transport_has_socket)
{
    auto peer = std::make_unique<NetworkTransport>();
    auto &sock = peer->socket();
    BOOST_TEST(!sock.is_open());
}

// -- Acceptor lifecycle --
BOOST_AUTO_TEST_CASE(acceptor_construct_with_port)
{
    NetworkTransport::Acceptor l{0};
    l.stop();
}

BOOST_AUTO_TEST_CASE(acceptor_move)
{
    NetworkTransport::Acceptor a{0};
    NetworkTransport::Acceptor b = std::move(a);
    b.stop();
}

BOOST_AUTO_TEST_CASE(acceptor_stop_idempotent)
{
    NetworkTransport::Acceptor l{0};
    l.stop();
    l.stop();
}

// -- Thread-safety smoke test for local transport --
BOOST_AUTO_TEST_CASE(local_transport_threaded_send)
{
    auto [a, b] = create_transport_pair();
    std::atomic<int> count{0};

    // Spawn reader first, then writer — must read concurrently to avoid
    // deadlock when channel capacity is exceeded.
    run_sync([&]() -> asio::awaitable<void> {
        auto reader = [&]() -> asio::awaitable<void> {
            for (int i = 0; i < 100; ++i) {
                co_await a->read();
                count++;
            }
        };
        auto writer = [&]() -> asio::awaitable<void> {
            for (int i = 0; i < 100; ++i)
                co_await b->write({NetPacket::chat, {}});
        };

        co_spawn(ITransport::io(), reader(), asio::detached);
        // Give reader a chance to start listening
        co_await asio::steady_timer(ITransport::io(),
                                    std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);
        co_await writer();
        // Small delay for remaining reads
        co_await asio::steady_timer(ITransport::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
    BOOST_TEST(count == 100);
}

BOOST_AUTO_TEST_CASE(disconnect_during_read)
{
    run_sync([]() -> asio::awaitable<void> {
        NetworkTransport::Acceptor acceptor{58888};
        std::unique_ptr<NetworkTransport> r, w;
        std::atomic<bool> connected{false};

        // Spawn accept, then connect — must run concurrently
        auto accept_coro = [&]() -> asio::awaitable<void> {
            auto t = co_await acceptor.accept();
            r.reset(dynamic_cast<NetworkTransport *>(t.release()));
            connected = true;
        };
        co_spawn(ITransport::io(), accept_coro(), asio::detached);

        // Give accept time to start listening
        co_await asio::steady_timer(ITransport::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
        w = co_await NetworkTransport::connect("127.0.0.1", 58888);
        BOOST_TEST(connected);

        // Now test: spawn reader, wait, close writer
        std::atomic<bool> read_failed{false};
        co_spawn(
            ITransport::io(),
            [&]() -> asio::awaitable<void> {
                try {
                    co_await r->read();
                }
                catch (std::exception const &) {
                    read_failed = true;
                }
            },
            asio::detached);

        co_await asio::steady_timer(ITransport::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
        w->socket().close();

        co_await asio::steady_timer(ITransport::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);

        BOOST_TEST(read_failed);
        BOOST_TEST(!r->is_connected());
    }());
}

BOOST_AUTO_TEST_SUITE_END()
