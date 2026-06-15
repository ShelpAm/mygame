#include "net/local-transport.hpp"
#include "net/net-packet.hpp"
#include "net/network-transport.hpp"
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
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
    auto payload = make_entity_update(7, 1.5f, -2.0f, 10, 20, true);
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
    auto payload = make_combat_event(1, 3, 25, true);
    auto ev = parse_combat_event(payload);
    BOOST_TEST(ev.attacker_id == 1);
    BOOST_TEST(ev.defender_id == 3);
    BOOST_TEST(ev.damage == 25);
    BOOST_TEST(ev.killed);
}

BOOST_AUTO_TEST_CASE(make_chat_preserves_text)
{
    auto payload = make_chat("hello world");
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
    BOOST_TEST(a->is_open());
    BOOST_TEST(b->is_open());
}

// -- NetworkTransport lifecycle --
BOOST_AUTO_TEST_CASE(network_transport_not_connected_initially)
{
    auto peer = std::make_shared<NetworkTransport>();
    BOOST_TEST(!peer->is_open());
}

BOOST_AUTO_TEST_CASE(network_transport_has_socket)
{
    auto peer = std::make_shared<NetworkTransport>();
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
        auto acceptor = std::make_shared<NetworkTransport::Acceptor>(58888);
        std::shared_ptr<NetworkTransport> r, w;
        std::atomic<bool> connected{false};

        // Spawn accept, then connect — must run concurrently
        auto accept_coro = [&]() -> asio::awaitable<void> {
            r = co_await acceptor->accept();
            connected = true;
        };
        co_spawn(ITransport::io(), accept_coro(), asio::detached);

        // Give accept time to start listening
        co_await asio::steady_timer(ITransport::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
        w = co_await NetworkTransport::connect("127.0.0.1", 58888);
        // Yield to let the accept coroutine post its completion
        co_await asio::steady_timer(ITransport::io(), std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);
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
        BOOST_TEST(!r->is_open());
    }());
}

BOOST_AUTO_TEST_CASE(close_wakes_read_loop)
{
    auto [a, b] = create_transport_pair();
    std::atomic<bool> read_closed{false};
    std::atomic<int> msg_count{0};

    co_spawn(
        ITransport::io(),
        [&](std::shared_ptr<ITransport> t) -> asio::awaitable<void> {
            try {
                while (true) {
                    co_await t->read();
                    msg_count++;
                }
            }
            catch (boost::system::system_error const &) {
                read_closed = true;
            }
        }(std::move(a)),
        asio::detached);

    // Write a message so we know the loop is running
    run_sync([&]() -> asio::awaitable<void> {
        co_await b->write({NetPacket::chat, {}});
    }());

    // Give the read loop time to process
    while (msg_count == 0)
        ITransport::io().poll_one();

    // Close the transport — should wake the read loop
    b->close();
    b.reset();

    // Drain io until the read coroutine exits
    while (!read_closed)
        ITransport::io().poll_one();

    BOOST_TEST(msg_count == 1);
    BOOST_TEST(read_closed);
}

// -- Auth handshake over local transport --

BOOST_AUTO_TEST_CASE(auth_handshake_success_over_local)
{
    auto [ca, cb] = create_transport_pair();

    bool server_ok = false, client_ok = false;
    run_sync([&]() -> asio::awaitable<void> {
        auto server_auth = [&]() -> asio::awaitable<void> {
            auto msg = co_await cb->read();
            if (msg.type == NetPacket::auth && msg.payload == auth_payload()) {
                co_await cb->write({NetPacket::auth, auth_payload()});
                server_ok = true;
            }
        };
        co_spawn(ITransport::io(), server_auth(), asio::detached);
        co_await asio::steady_timer(ITransport::io(), std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);
        co_await ca->write({NetPacket::auth, auth_payload()});
        auto res = co_await ca->read();
        client_ok = (res.type == NetPacket::auth && res.payload == auth_payload());
        co_await asio::steady_timer(ITransport::io(), std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
    BOOST_TEST(client_ok);
    BOOST_TEST(server_ok);
}

// -- Packet make+parse roundtrips --

BOOST_AUTO_TEST_CASE(make_and_parse_player_input)
{
    auto payload = make_player_input(42, 1.5f, -3.0f);
    auto in = parse_player_input(payload);
    BOOST_TEST(in.pid == 42);
    BOOST_TEST(in.mx == 1.5f);
    BOOST_TEST(in.my == -3.0f);
}

BOOST_AUTO_TEST_CASE(make_and_parse_entity_removed)
{
    auto payload = make_entity_removed(0xDEADBEEF);
    BOOST_TEST(payload.size() == 8);
    EntityId eid;
    memcpy(&eid, payload.data(), 8);
    BOOST_TEST(eid == 0xDEADBEEF);
}

BOOST_AUTO_TEST_CASE(make_return_pid_roundtrip)
{
    auto payload = make_return_pid(99, 3);
    BOOST_TEST(payload.size() == 9);
    EntityId eid;
    memcpy(&eid, payload.data(), 8);
    BOOST_TEST(eid == 99);
    BOOST_TEST(payload[8] == 3);
}

BOOST_AUTO_TEST_CASE(make_entity_id_payload_roundtrip)
{
    auto payload = make_entity_id_payload(0xABCD);
    BOOST_TEST(payload.size() == 8);
    EntityId eid;
    memcpy(&eid, payload.data(), 8);
    BOOST_TEST(eid == 0xABCD);
}

// -- Full auth → join → return_pid over local transport --

BOOST_AUTO_TEST_CASE(full_auth_and_join_local)
{
    auto [ca, cb] = create_transport_pair();

    EntityId returned_pid = invalid_entity;
    bool join_received = false;

    run_sync([&]() -> asio::awaitable<void> {
        auto server_task = [&]() -> asio::awaitable<void> {
            // Auth
            auto req = co_await cb->read();
            BOOST_TEST(req.type == NetPacket::auth);
            co_await cb->write({NetPacket::auth, auth_payload()});
            // Read join
            auto join = co_await cb->read();
            BOOST_TEST(join.type == NetPacket::join);
            join_received = true;
            // Send return_pid
            co_await cb->write({NetPacket::return_pid, make_return_pid(123, 2)});
        };
        co_spawn(ITransport::io(), server_task(), asio::detached);
        co_await asio::steady_timer(ITransport::io(), std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);

        // Client: auth → send join → read return_pid
        co_await ca->write({NetPacket::auth, auth_payload()});
        auto auth_res = co_await ca->read();
        BOOST_TEST(auth_res.type == NetPacket::auth);
        co_await ca->write({NetPacket::join, {}});
        auto pid_res = co_await ca->read();
        BOOST_TEST(pid_res.type == NetPacket::return_pid);
        BOOST_TEST(pid_res.payload.size() >= 9);
        memcpy(&returned_pid, pid_res.payload.data(), 8);

        co_await asio::steady_timer(ITransport::io(), std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
    BOOST_TEST(join_received);
    BOOST_TEST(returned_pid == 123);
}

// -- Full TCP auth → join → return_pid --

BOOST_AUTO_TEST_CASE(network_transport_auth_and_join)
{
    static constexpr uint16_t kPort = 58889;

    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkTransport::Acceptor>(kPort);

        EntityId returned_pid = invalid_entity;

        // Server side
        auto server_task = [&]() -> asio::awaitable<void> {
            auto t = co_await acceptor->accept();
            auto req = co_await t->read();
            BOOST_TEST(req.type == NetPacket::auth);
            co_await t->write({NetPacket::auth, auth_payload()});
            auto join = co_await t->read();
            BOOST_TEST(join.type == NetPacket::join);
            co_await t->write({NetPacket::return_pid, make_return_pid(456, 1)});
        };
        co_spawn(ITransport::io(), server_task(), asio::detached);

        // Client side
        auto peer = co_await NetworkTransport::connect("127.0.0.1", kPort);
        co_await peer->write({NetPacket::auth, auth_payload()});
        auto auth_res = co_await peer->read();
        BOOST_TEST(auth_res.type == NetPacket::auth);
        co_await peer->write({NetPacket::join, {}});
        auto pid_res = co_await peer->read();
        BOOST_TEST(pid_res.type == NetPacket::return_pid);
        BOOST_TEST(pid_res.payload.size() >= 9);
        memcpy(&returned_pid, pid_res.payload.data(), 8);
        BOOST_TEST(returned_pid == 456);

        co_await asio::steady_timer(ITransport::io(), std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
