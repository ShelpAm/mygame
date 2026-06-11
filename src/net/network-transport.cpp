#include "net/network-transport.hpp"
#include "net/net-packet.hpp"
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>

NetworkTransport::Acceptor::Acceptor(std::uint16_t port)
    : impl_(std::make_unique<tcp::acceptor>(NetworkTransport::io(),
                                            tcp::endpoint(tcp::v4(), port)))
{
}

awaitable<std::unique_ptr<ITransport>> NetworkTransport::Acceptor::accept()
{
    co_return co_await impl_->async_accept(use_awaitable);
}

// -- io --
static std::unique_ptr<boost::asio::io_context> s_io;
static std::optional<decltype(boost::asio::make_work_guard(*s_io))> s_guard;
static std::thread s_thread;

static void ensure_io()
{
    if (s_io)
        return;
    s_io = std::make_unique<boost::asio::io_context>();
    s_guard.emplace(boost::asio::make_work_guard(*s_io));
    s_thread = std::thread([]() {
        try {
            s_io->run();
        }
        catch (std::exception const &e) {
            spdlog::error("IO error: {}", e.what());
        }
    });
}

boost::asio::io_context &NetworkTransport::io()
{
    ensure_io();
    return *s_io;
}

void NetworkTransport::shutdown()
{
    if (!s_io)
        return;
    s_guard.reset();
    s_io->stop();
    if (s_thread.joinable())
        s_thread.join();
    s_io.reset();
}

awaitable<std::unique_ptr<NetworkTransport>>
NetworkTransport::connect(std::string const &ip, int port)
{
    try {
        auto peer = std::make_unique<NetworkTransport>();
        co_await peer->socket().async_connect(
            tcp::endpoint(boost::asio::ip::make_address(ip), port),
            boost::asio::use_awaitable);
        peer->on_connected();
        spdlog::info("Connected!");
        co_return peer;
    }
    catch (std::exception &e) {
        spdlog::error("Connect failed: {}", e.what());
        throw;
    }
}

// -- ITransport impl --
void NetworkTransport::send(TransportMessage msg)
{
    auto data = std::make_shared<std::vector<uint8_t>>(
        serialize_packet({msg.type, std::move(msg.payload)}));
    boost::asio::post(io(), [this, data]() {
        if (!socket_.is_open())
            return;
        boost::asio::async_write(socket_, boost::asio::buffer(*data),
                                 [data](boost::system::error_code, size_t) {});
    });
}

void NetworkTransport::set_callback(Callback cb)
{
    callback_ = std::move(cb);
}

void NetworkTransport::consume()
{
    std::vector<TransportMessage> batch;
    {
        std::lock_guard lock(mutex_);
        batch.swap(recv_queue_);
    }
    for (auto &msg : batch) {
        if (callback_)
            callback_({this, msg.type, std::move(msg.payload)});
    }
}

bool NetworkTransport::is_connected() const
{
    return connected_;
}

void NetworkTransport::on_connected()
{
    connected_ = true;
    start_read();
}

void NetworkTransport::start_read()
{
    auto head = std::make_shared<std::array<uint8_t, 8>>();
    boost::asio::async_read(
        socket_, boost::asio::buffer(*head),
        [this, head](boost::system::error_code ec, size_t) {
            if (ec) {
                connected_ = false;
                return;
            }
            uint32_t type, size;
            memcpy(&type, head->data(), 4);
            memcpy(&size, head->data() + 4, 4);

            auto body = std::make_shared<std::vector<uint8_t>>(size);
            boost::asio::async_read(
                socket_, boost::asio::buffer(*body),
                [this, type, body](boost::system::error_code ec2, size_t) {
                    if (ec2) {
                        connected_ = false;
                        return;
                    }
                    {
                        std::lock_guard lock(mutex_);
                        recv_queue_.push_back(
                            {static_cast<NetPacket::Type>(type),
                             std::move(*body)});
                    }
                    start_read();
                });
        });
}
