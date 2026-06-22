#include "net-fixture.hpp"

BOOST_AUTO_TEST_SUITE(local_session_tests)

BOOST_AUTO_TEST_CASE(local_transport_send_receive)
{
    auto [a, b] = create_local_transport_pair();
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
    auto [a, b] = create_local_transport_pair();

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
    auto [a, b] = create_local_transport_pair();

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
    auto [a, b] = create_local_transport_pair();
    BOOST_TEST(a->is_open());
    BOOST_TEST(b->is_open());
}

BOOST_AUTO_TEST_CASE(local_transport_write_to_dead_peer_throws)
{
    auto [a, b] = create_local_transport_pair();
    run_sync([&]() -> asio::awaitable<void> {
        b.reset();
        BOOST_CHECK_THROW(co_await a->write({NetPacket::chat, {}}), std::runtime_error);
    }());
}

BOOST_AUTO_TEST_CASE(local_transport_is_open_after_peer_destruction)
{
    auto [a, b] = create_local_transport_pair();
    BOOST_TEST(a->is_open());
    BOOST_TEST(b->is_open());
    b.reset();
    BOOST_TEST(!a->is_open());
}

BOOST_AUTO_TEST_CASE(local_transport_remote_info_format)
{
    auto [a, b] = create_local_transport_pair();
    auto info = b->remote_info();
    BOOST_TEST(info.find("local") != std::string::npos);
    BOOST_TEST(info.find("0x") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(local_transport_threaded_send)
{
    auto [a, b] = create_local_transport_pair();
    std::atomic<int> count{0};

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

        co_spawn(Session::io(), reader(), asio::detached);
        co_await asio::steady_timer(Session::io(), std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);
        co_await writer();
        co_await asio::steady_timer(Session::io(), std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
    BOOST_TEST(count == 100);
}

BOOST_AUTO_TEST_CASE(close_wakes_read_loop)
{
    auto [a, b] = create_local_transport_pair();
    std::atomic<bool> read_closed{false};
    std::atomic<int> msg_count{0};

    co_spawn(
        Session::io(),
        [&](std::shared_ptr<Session> t) -> asio::awaitable<void> {
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

    run_sync([&]() -> asio::awaitable<void> { co_await b->write({NetPacket::chat, {}}); }());

    while (msg_count == 0)
        Session::io().poll_one();

    b->close();
    b.reset();

    while (!read_closed)
        Session::io().poll_one();

    BOOST_TEST(msg_count == 1);
    BOOST_TEST(read_closed);
}

BOOST_AUTO_TEST_CASE(auth_handshake_success_over_local)
{
    auto [ca, cb] = create_local_transport_pair();

    bool server_ok = false, client_ok = false;
    run_sync([&]() -> asio::awaitable<void> {
        auto server_auth = [&]() -> asio::awaitable<void> {
            auto msg = co_await cb->read();
            if (msg.type == NetPacket::auth && msg.payload == auth_payload()) {
                co_await cb->write({NetPacket::auth, auth_payload()});
                server_ok = true;
            }
        };
        co_spawn(Session::io(), server_auth(), asio::detached);
        co_await asio::steady_timer(Session::io(), std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);
        co_await ca->write({NetPacket::auth, auth_payload()});
        auto res = co_await ca->read();
        client_ok = (res.type == NetPacket::auth && res.payload == auth_payload());
        co_await asio::steady_timer(Session::io(), std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
    BOOST_TEST(client_ok);
    BOOST_TEST(server_ok);
}

BOOST_AUTO_TEST_CASE(full_auth_and_join_local)
{
    auto [ca, cb] = create_local_transport_pair();

    EntityId returned_pid = invalid_entity;
    bool join_received = false;

    run_sync([&]() -> asio::awaitable<void> {
        auto server_task = [&]() -> asio::awaitable<void> {
            auto req = co_await cb->read();
            BOOST_TEST(req.type == NetPacket::auth);
            co_await cb->write({NetPacket::auth, auth_payload()});
            auto join = co_await cb->read();
            BOOST_TEST(join.type == NetPacket::join);
            join_received = true;
            co_await cb->write({NetPacket::return_pid, make_return_pid(123, 2)});
        };
        co_spawn(Session::io(), server_task(), asio::detached);
        co_await asio::steady_timer(Session::io(), std::chrono::milliseconds(1))
            .async_wait(asio::use_awaitable);

        co_await ca->write({NetPacket::auth, auth_payload()});
        auto auth_res = co_await ca->read();
        BOOST_TEST(auth_res.type == NetPacket::auth);
        co_await ca->write({NetPacket::join, {}});
        auto pid_res = co_await ca->read();
        BOOST_TEST(pid_res.type == NetPacket::return_pid);
        BOOST_TEST(pid_res.payload.size() >= 9);
        memcpy(&returned_pid, pid_res.payload.data(), 8);

        co_await asio::steady_timer(Session::io(), std::chrono::milliseconds(10))
            .async_wait(asio::use_awaitable);
    }());
    BOOST_TEST(join_received);
    BOOST_TEST(returned_pid == 123);
}

BOOST_AUTO_TEST_SUITE_END()
