#include "net/network-transport.hpp"
#include "net/net-packet.hpp"
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cstring>
#include <spdlog/spdlog.h>

using namespace asio::experimental::awaitable_operators;

NetworkTransport::NetworkTransport(tcp::socket sock) : socket_(std::move(sock))
{
    // on_connected();
}

NetworkTransport::Acceptor::Acceptor(std::uint16_t port)
    : impl_(std::make_shared<tcp::acceptor>(ITransport::io(),
                                            tcp::endpoint(tcp::v4(), port)))
{
}

awaitable<std::unique_ptr<ITransport>> NetworkTransport::Acceptor::accept()
{
    auto sock = co_await impl_->async_accept(use_awaitable);
    co_return std::make_unique<NetworkTransport>(std::move(sock));
}

void NetworkTransport::Acceptor::stop()
{
    if (impl_ && impl_->is_open())
        impl_->close();
}

awaitable<std::unique_ptr<NetworkTransport>>
NetworkTransport::connect(std::string const &ip, int port)
{
    spdlog::debug("Initiating connection to {}:{}", ip, port);
    auto t = std::make_unique<NetworkTransport>();
    auto ep = tcp::endpoint(asio::ip::make_address(ip), port);

    asio::steady_timer timer(io(), std::chrono::seconds(5));
    // timer.async_wait([raw = t.get()](boost::system::error_code ec) {
    //     if (!ec)
    //         raw->socket().cancel();
    // });

    // auto [ec] = co_await t->socket().async_connect(
    //     ep, asio::as_tuple(use_awaitable));
    // timer.cancel(); // Run timer

    auto r = co_await (t->socket().async_connect(ep, as_tuple(use_awaitable)) ||
                       timer.async_wait(as_tuple(use_awaitable)));

    if (r.index() == 1)
        throw std::runtime_error("Connection timed out after 5s");

    assert(r.index() == 0);
    if (auto [ec] = std::get<0>(r); ec)
        throw std::runtime_error("Connection failed: " + ec.message());

    // t->on_connected();
    spdlog::info("Connected!");
    co_return t;
}

// -- ITransport impl --
awaitable<void> NetworkTransport::write(TransportMessage msg)
{
    if (!socket_.is_open())
        throw std::runtime_error("NetworkTransport::write: socket is not open");

    write_buffer_ = serialize_packet({msg.type, std::move(msg.payload)});
    co_await asio::async_write(socket_, asio::buffer(write_buffer_),
                               use_awaitable);
}

awaitable<TransportMessage> NetworkTransport::read()
{
    co_await asio::async_read(
        socket_, asio::buffer(&read_head_buffer_, sizeof(read_head_buffer_)),
        use_awaitable);
    read_body_buffer_.resize(read_head_buffer_.size);
    co_await asio::async_read(socket_, asio::buffer(read_body_buffer_),
                              use_awaitable);

    co_return TransportMessage{.type = read_head_buffer_.type,
                               .payload = read_body_buffer_};
    // std::vector<TransportMessage> batch;
    // {
    //     std::lock_guard lock(mutex_);
    //     batch.swap(recv_queue_);
    // }
    // for (auto &msg : batch) {
    //     if (callback_)
    //         callback_({this, msg.type, std::move(msg.payload)});
    // }
}

bool NetworkTransport::is_connected() const
{
    return connected_;
}

void NetworkTransport::on_connected()
{
    throw std::runtime_error(
        "Don't call on_connected, it should migrate to coro read");
    connected_ = true;
    start_read();
}

void NetworkTransport::start_read()
{
    auto head = std::make_shared<std::array<uint8_t, 8>>();
    asio::async_read(
        socket_, asio::buffer(*head),
        [this, head](boost::system::error_code ec, size_t) {
            if (ec == asio::error::eof) {
            }
            if (ec) {
                spdlog::warn("async_read header error: {}", ec.message());
                connected_ = false;
                return;
            }
            uint32_t type, size;
            memcpy(&type, head->data(), 4);
            memcpy(&size, head->data() + 4, 4);

            auto body = std::make_shared<std::vector<uint8_t>>(size);
            asio::async_read(
                socket_, asio::buffer(*body),
                [this, type, body](boost::system::error_code ec2, size_t) {
                    if (ec2) {
                        spdlog::warn("async_read body error: {}",
                                     ec2.message());
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
