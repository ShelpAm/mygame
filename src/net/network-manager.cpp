#include "net/network-manager.hpp"
#include "net/net-packet.hpp"
#include <cstring>
#include <iostream>
#include <print>

NetworkManager::NetworkManager()
    : thread_{std::thread(&NetworkManager::io_thread, this)},
      state_{ConnectionState::disconnected}
{
}

NetworkManager::~NetworkManager()
{
    io_.stop();
    if (thread_.joinable())
        thread_.join();
}

bool NetworkManager::host(int port)
{
    try {
        acceptor_ = std::make_unique<tcp::acceptor>(
            io_, tcp::endpoint(tcp::v4(), port));
        if (!socket_)
            socket_ = std::make_unique<tcp::socket>(io_);
        acceptor_->async_accept(*socket_, [this](boost::system::error_code ec) {
            if (ec)
                throw boost::system::system_error(ec);
            std::println("Client connected");
            start_read();
        });

        std::println("Hosting on port {}", port);
        hosting_ = true;
        return true;
    }
    catch (std::exception const &e) {
        std::cerr << "Host failed: " << e.what() << '\n';
        return false;
    }
}

bool NetworkManager::connect(std::string const &ip, int port)
{
    try {
        state_ = ConnectionState::connecting;
        socket_ = std::make_unique<tcp::socket>(io_);
        socket_->async_connect(
            tcp::endpoint(boost::asio::ip::make_address(ip), port),
            [this](boost::system::error_code ec) {
                if (ec) {
                    state_ = ConnectionState::disconnected;
                    std::cerr << "Connect failed: " << ec.message() << '\n';
                    return;
                }
                state_ = ConnectionState::connected;
                std::cout << "Connected!\n";
                start_read();
            });
        return true;
    }
    catch (std::exception const &e) {
        std::cerr << "Connect failed: " << e.what() << '\n';
        return false;
    }
}

void NetworkManager::io_thread()
{
    try {
        io_.run();
    }
    catch (std::exception const &e) {
        assert(false && "NetworkManager::io_thread threw an exception");
        std::cerr << "IO error: " << e.what() << '\n';
        state_ = ConnectionState::disconnected;
    }
}

void NetworkManager::disconnect()
{
    if (hosting_) {
        if (acceptor_)
            acceptor_->close();
        hosting_ = false;
    }
    socket_.reset();
    state_ = ConnectionState::disconnected;
}

void NetworkManager::start_read()
{
    auto buf = std::make_shared<std::array<uint8_t, 8>>();
    boost::asio::async_read( // Reads head
        *socket_, boost::asio::buffer(*buf),
        [this, buf](boost::system::error_code ec, size_t) {
            if (ec)
                return;
            assert(state_ == ConnectionState::connected);
            uint32_t msgType, size;
            memcpy(&msgType, buf->data(), 4);
            memcpy(&size, buf->data() + 4, 4);

            // Continues to read body
            auto data = std::make_shared<std::vector<uint8_t>>(size);
            boost::asio::async_read(
                *socket_, boost::asio::buffer(*data),
                [this, msgType, data](boost::system::error_code ec2, size_t) {
                    if (ec2) {
                        state_ = ConnectionState::disconnected;
                        return;
                    }
                    NetPacket pkt{static_cast<NetPacket::Type>(msgType), *data};
                    if (callback_)
                        callback_(pkt);
                });
            start_read();
        });
}

void NetworkManager::queue_send(std::vector<uint8_t> data)
{
    if (state_ != ConnectionState::connected)
        return;
    assert(socket_ && socket_->is_open());
    auto buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
    boost::asio::async_write(*socket_, boost::asio::buffer(*buf),
                             [buf](boost::system::error_code, size_t) {});
}

void NetworkManager::update() {}
