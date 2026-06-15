#include "net/network-transport.hpp"
#include "net/net-packet.hpp"
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <ranges>
#include <spdlog/spdlog.h>

using namespace asio::experimental::awaitable_operators;

NetworkTransport::NetworkTransport(tcp::socket sock) : socket_(std::move(sock))
{
    if (!socket_.is_open())
        throw std::runtime_error("NetworkTransport: socket is not open (why do "
                                 "you pass an closed/uninit socket?)");

    cached_socket_info_ =
        std::format("{}:{}", socket_.remote_endpoint().address().to_string(),
                    std::to_string(socket_.remote_endpoint().port()));
}

NetworkTransport::Acceptor::Acceptor(std::uint16_t port)
    : impl_(std::make_shared<tcp_acceptor>(ITransport::io(),
                                           tcp::endpoint(tcp::v4(), port)))
{
}

awaitable<std::unique_ptr<NetworkTransport>>
NetworkTransport::Acceptor::accept()
{
    auto sock = co_await impl_->async_accept();
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
    spdlog::info("NetworkTransport: initiating connection to {}:{}", ip, port);
    auto t = std::make_unique<NetworkTransport>();
    auto ep = tcp::endpoint(asio::ip::make_address(ip), port);

    asio::steady_timer timer(io(), std::chrono::seconds(5));

    auto r = co_await (t->socket().async_connect(ep, as_tuple(use_awaitable)) ||
                       timer.async_wait(as_tuple(use_awaitable)));

    if (r.index() == 1) {
        auto [ec] = std::get<1>(r);
        assert(!ec);
        throw std::runtime_error("Connection timed out after 5s");
    }

    if (r.index() == 0) {
        if (auto [ec] = std::get<0>(r); ec)
            throw std::runtime_error("Connection failed: " + ec.message());
    }

    t->cached_socket_info_ = std::format(
        "{}:{}", t->socket().remote_endpoint().address().to_string(),
        std::to_string(t->socket().remote_endpoint().port()));
    spdlog::info("NetworkTransport: connected to {}",
                 t->socket_.remote_endpoint().address().to_string());
    co_return t;
}

NetworkTransport::~NetworkTransport()
{
    close();
}

awaitable<void> NetworkTransport::write(TransportMessage msg)
{
    if (!socket_.is_open())
        throw std::runtime_error("NetworkTransport::write: socket is not open");

    spdlog::log(msg.type == NetPacket::state_delta ? spdlog::level::trace
                                                   : spdlog::level::debug,
                "NetwortTransport {} ({}) writing message of type \"{}\" with "
                "payload size {}",
                remote_info(), (void *)this, msg.type, msg.payload.size());
    write_buffer_ = serialize_packet({msg.type, std::move(msg.payload)});
    co_await asio::async_write(socket_, asio::buffer(write_buffer_));
}

awaitable<TransportMessage> NetworkTransport::read()
{
    auto [ec_head, _] = co_await asio::async_read(
        socket_, asio::buffer(&read_head_buffer_, sizeof(read_head_buffer_)),
        as_tuple(use_awaitable));

    if (ec_head) {
        socket_.close();
        throw boost::system::system_error(ec_head);
    }

    read_body_buffer_.resize(read_head_buffer_.size);
    auto [ec_body, _] = co_await asio::async_read(
        socket_, asio::buffer(read_body_buffer_), as_tuple(use_awaitable));
    if (ec_body) {
        socket_.close();
        throw boost::system::system_error(ec_head);
    }

    co_return TransportMessage{.type = read_head_buffer_.type,
                               .payload = read_body_buffer_};
}

bool NetworkTransport::is_open() const
{
    return socket_.is_open();
}

void NetworkTransport::close()
{
    cached_socket_info_ += " (closed)";
    socket_.close();
}

tcp_socket &NetworkTransport::socket()
{
    return socket_;
}
std::string NetworkTransport::remote_info() const
{
    // if (!is_open())
    //     throw std::runtime_error(
    //         "NetworkTransport: unknown remote_info on closed transport");

    return cached_socket_info_;
}
