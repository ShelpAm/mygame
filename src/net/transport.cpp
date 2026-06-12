#include "net/transport.hpp"
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>

namespace asio = boost::asio;

// -- io --
static std::unique_ptr<asio::io_context> s_io;
static std::optional<decltype(asio::make_work_guard(*s_io))> s_guard;
static std::jthread s_thread;

static void ensure_io()
{
    if (s_io)
        return;
    spdlog::info("Networktransport IO thread not running, starting...");
    s_io = std::make_unique<asio::io_context>();
    s_guard.emplace(asio::make_work_guard(*s_io));
    s_thread = std::jthread([]() {
        try {
            spdlog::info("  IO thread Started");
            s_io->run();
            spdlog::info("  IO thread stopped");
        }
        catch (std::exception const &e) {
            spdlog::error("IO error: {}", e.what());
        }
    });
}

asio::io_context &ITransport::io()
{
    ensure_io();
    return *s_io;
}

void ITransport::shutdown()
{
    if (!s_io)
        return;

    spdlog::info("Shutting down NetworkTransport IO thread");
    s_guard.reset();
    s_io->stop();
    if (s_thread.joinable())
        s_thread.join();
    s_io.reset();
    spdlog::info("Done");
}
