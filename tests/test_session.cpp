#include "net-fixture.hpp"

BOOST_AUTO_TEST_SUITE(session_tests)

BOOST_AUTO_TEST_CASE(session_set_io_and_io_access)
{
    auto *saved = &Session::io();
    asio::io_context local_io;
    Session::set_io(&local_io);
    BOOST_TEST(&Session::io() == &local_io);
    Session::set_io(saved);
}

BOOST_AUTO_TEST_CASE(session_io_before_set_throws)
{
    auto &ref = Session::io();
    auto *saved = &ref;
    Session::set_io(nullptr);
    BOOST_CHECK_THROW(Session::io(), std::runtime_error);
    Session::set_io(saved);
}

BOOST_AUTO_TEST_CASE(session_spawn_completes_coroutine)
{
    std::atomic<bool> executed{false};
    Session::spawn([&]() -> asio::awaitable<void> {
        executed = true;
        co_return;
    }());
    Session::io().poll_one();
    BOOST_TEST(executed);
}

BOOST_AUTO_TEST_CASE(session_spawn_handles_exception)
{
    std::atomic<bool> after{false};
    Session::spawn([]() -> asio::awaitable<void> {
        throw std::runtime_error("intentional test exception");
        co_return;
    }());
    asio::post(Session::io(), [&]() { after = true; });
    Session::io().poll_one();
    Session::io().poll_one();
    BOOST_TEST(after);
}

BOOST_AUTO_TEST_CASE(session_interface_via_mock)
{
    auto mock = std::make_shared<MockSession>();
    Session *iface = mock.get();
    BOOST_TEST(iface->is_open());
    BOOST_TEST(iface->remote_info() == "mock");
    iface->close();
    run_sync([&]() -> asio::awaitable<void> {
        co_await iface->write({ClientMsgType::chat, {}});
        auto msg = co_await iface->read();
        BOOST_TEST(msg.type == uint32_t{});
        BOOST_TEST(msg.payload.empty());
    }());
}

BOOST_AUTO_TEST_SUITE_END()
