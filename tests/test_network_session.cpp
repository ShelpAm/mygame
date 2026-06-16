#include "net-fixture.hpp"
#include <cstring>

BOOST_AUTO_TEST_SUITE(network_session_tests)

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
    BOOST_TEST(data.size() == 8 + payload.size());
    auto type = read_bytes<uint32_t>(data, 0);
    auto size = read_bytes<uint32_t>(data, 4);
    BOOST_TEST(type == static_cast<uint32_t>(NetPacket::chat));
    BOOST_TEST(size == payload.size());
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

// -- NetworkSession lifecycle --
BOOST_AUTO_TEST_CASE(network_transport_not_connected_initially)
{
    auto peer = std::make_shared<NetworkSession>();
    BOOST_TEST(!peer->is_open());
}

BOOST_AUTO_TEST_CASE(network_transport_has_socket)
{
    auto peer = std::make_shared<NetworkSession>();
    auto &sock = peer->socket();
    BOOST_TEST(!sock.is_open());
}

// -- Acceptor lifecycle --
BOOST_AUTO_TEST_CASE(acceptor_construct_with_port)
{
    NetworkSession::Acceptor l{0};
    l.stop();
}

BOOST_AUTO_TEST_CASE(acceptor_move)
{
    NetworkSession::Acceptor a{0};
    NetworkSession::Acceptor b = std::move(a);
    b.stop();
}

BOOST_AUTO_TEST_CASE(acceptor_stop_idempotent)
{
    NetworkSession::Acceptor l{0};
    l.stop();
    l.stop();
}

// -- NetworkSession accept/connect roundtrip --
BOOST_AUTO_TEST_CASE(network_transport_accept_connect_roundtrip)
{
    static constexpr uint16_t kPort = 58890;
    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(kPort);
        std::shared_ptr<NetworkSession> server_side;

        auto server_task = [&]() -> asio::awaitable<void> {
            server_side = co_await acceptor->accept();
            co_await server_side->write({NetPacket::chat, {}});
        };
        co_spawn(Session::io(), server_task(), asio::detached);
        auto client_side = co_await NetworkSession::connect("127.0.0.1", kPort);
        auto msg = co_await client_side->read();
        BOOST_TEST(msg.type == NetPacket::chat);

        BOOST_TEST(server_side != nullptr);
        BOOST_TEST(server_side->is_open());
        BOOST_TEST(client_side->is_open());
        acceptor->stop();
    }());
}

BOOST_AUTO_TEST_CASE(network_transport_write_read_roundtrip)
{
    static constexpr uint16_t kPort = 58891;
    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(kPort);
        std::shared_ptr<NetworkSession> server_side;

        auto server_task = [&]() -> asio::awaitable<void> {
            server_side = co_await acceptor->accept();
            auto req = co_await server_side->read();
            co_await server_side->write({req.type, std::move(req.payload)});
        };
        co_spawn(Session::io(), server_task(), asio::detached);
        auto client_side = co_await NetworkSession::connect("127.0.0.1", kPort);
        std::vector<uint8_t> payload = {0xde, 0xad, 0xbe, 0xef};
        co_await client_side->write({NetPacket::chat, payload});
        auto msg = co_await client_side->read();
        BOOST_TEST(msg.type == NetPacket::chat);
        BOOST_TEST(msg.payload == payload);
        acceptor->stop();
    }());
}

BOOST_AUTO_TEST_CASE(network_transport_remote_info_on_connected)
{
    static constexpr uint16_t kPort = 58892;
    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(kPort);

        auto server_task = [&]() -> asio::awaitable<void> {
            auto s = co_await acceptor->accept();
            co_await s->write({NetPacket::chat, {}});
        };
        co_spawn(Session::io(), server_task(), asio::detached);
        auto peer = co_await NetworkSession::connect("127.0.0.1", kPort);
        co_await peer->read();
        auto info = peer->remote_info();
        BOOST_TEST(!info.empty());
        BOOST_TEST(info.find("127.0.0.1") != std::string::npos);
        acceptor->stop();
    }());
}

BOOST_AUTO_TEST_CASE(network_transport_close_on_connected)
{
    static constexpr uint16_t kPort = 58893;
    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(kPort);

        auto server_task = [&]() -> asio::awaitable<void> {
            auto s = co_await acceptor->accept();
            co_await s->write({NetPacket::chat, {}});
        };
        co_spawn(Session::io(), server_task(), asio::detached);
        auto peer = co_await NetworkSession::connect("127.0.0.1", kPort);
        co_await peer->read();
        BOOST_TEST(peer->is_open());
        peer->close();
        BOOST_TEST(!peer->is_open());
        acceptor->stop();
    }());
}

BOOST_AUTO_TEST_CASE(network_transport_write_after_close_throws)
{
    static constexpr uint16_t kPort = 58894;
    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(kPort);

        auto server_task = [&]() -> asio::awaitable<void> {
            auto s = co_await acceptor->accept();
            co_await s->write({NetPacket::chat, {}});
        };
        co_spawn(Session::io(), server_task(), asio::detached);
        auto peer = co_await NetworkSession::connect("127.0.0.1", kPort);
        co_await peer->read();
        peer->close();
        BOOST_CHECK_THROW(
            co_await peer->write({NetPacket::chat, {}}), std::runtime_error);
        acceptor->stop();
    }());
}

BOOST_AUTO_TEST_CASE(network_transport_read_after_close_throws)
{
    BOOST_CHECK_THROW(
        (void)run_sync([]() -> asio::awaitable<void> {
            auto peer = std::make_shared<NetworkSession>();
            peer->close();
            co_await peer->read();
        }()),
        boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(network_transport_connection_timeout)
{
    BOOST_CHECK_THROW(
        (void)run_sync(NetworkSession::connect("10.255.255.1", 9999)),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(network_transport_socket_accessor_connected)
{
    static constexpr uint16_t kPort = 58895;
    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(kPort);

        auto server_task = [&]() -> asio::awaitable<void> {
            auto s = co_await acceptor->accept();
            co_await s->write({NetPacket::chat, {}});
        };
        co_spawn(Session::io(), server_task(), asio::detached);
        auto peer = co_await NetworkSession::connect("127.0.0.1", kPort);
        co_await peer->read();
        BOOST_TEST(peer->socket().is_open());
        acceptor->stop();
    }());
}

BOOST_AUTO_TEST_CASE(disconnect_during_read)
{
    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(58888);
        std::shared_ptr<NetworkSession> r, w;
        std::atomic<bool> connected{false};

        auto accept_coro = [&]() -> asio::awaitable<void> {
            r = co_await acceptor->accept();
            connected = true;
        };
        co_spawn(Session::io(), accept_coro(), asio::detached);

        co_await asio::steady_timer(Session::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
        w = co_await NetworkSession::connect("127.0.0.1", 58888);
        co_await asio::steady_timer(Session::io(),
                                    std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);
        BOOST_TEST(connected);

        std::atomic<bool> read_failed{false};
        co_spawn(
            Session::io(),
            [&]() -> asio::awaitable<void> {
                try {
                    co_await r->read();
                }
                catch (std::exception const &) {
                    read_failed = true;
                }
            },
            asio::detached);

        co_await asio::steady_timer(Session::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
        w->socket().close();

        co_await asio::steady_timer(Session::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);

        BOOST_TEST(read_failed);
        BOOST_TEST(!r->is_open());
    }());
}

BOOST_AUTO_TEST_CASE(network_transport_auth_and_join)
{
    static constexpr uint16_t kPort = 58889;

    run_sync([]() -> asio::awaitable<void> {
        auto acceptor = std::make_shared<NetworkSession::Acceptor>(kPort);

        EntityId returned_pid = invalid_entity;

        auto server_task = [&]() -> asio::awaitable<void> {
            auto t = co_await acceptor->accept();
            auto req = co_await t->read();
            BOOST_TEST(req.type == NetPacket::auth);
            co_await t->write({NetPacket::auth, auth_payload()});
            auto join = co_await t->read();
            BOOST_TEST(join.type == NetPacket::join);
            co_await t->write({NetPacket::return_pid, make_return_pid(456, 1)});
        };
        co_spawn(Session::io(), server_task(), asio::detached);

        auto peer = co_await NetworkSession::connect("127.0.0.1", kPort);
        co_await peer->write({NetPacket::auth, auth_payload()});
        auto auth_res = co_await peer->read();
        BOOST_TEST(auth_res.type == NetPacket::auth);
        co_await peer->write({NetPacket::join, {}});
        auto pid_res = co_await peer->read();
        BOOST_TEST(pid_res.type == NetPacket::return_pid);
        BOOST_TEST(pid_res.payload.size() >= 9);
        memcpy(&returned_pid, pid_res.payload.data(), 8);
        BOOST_TEST(returned_pid == 456);

        co_await asio::steady_timer(Session::io(),
                                    std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
