#pragma once

#include "net/net-packet.hpp"

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

enum class ConnectionState {
    disconnected,
    connecting,
    connected,
};

class NetworkManager {
  public:
    using MsgCallback = std::function<void(NetPacket const &)>;

    NetworkManager();
    ~NetworkManager();

    bool host(int port = 27015);
    bool connect(std::string const &ip, int port = 27015);
    void disconnect(); // Disconnects from server
    bool is_connected() const
    {
        return state_ == ConnectionState::connected;
    }
    bool is_hosting() const
    {
        return hosting_;
    }

    void update();
    void queue_send(std::vector<uint8_t> data);
    void set_callback(MsgCallback cb)
    {
        callback_ = std::move(cb);
    }

  private:
    void io_thread();
    void start_read();

    boost::asio::io_context io_;
    decltype(boost::asio::make_work_guard(io_)) work_guard_ =
        boost::asio::make_work_guard(io_);
    std::thread thread_;

    // Both sides
    MsgCallback callback_;

    // Server side
    std::unique_ptr<tcp::acceptor> acceptor_;
    bool hosting_ = false;

    // Client side
    std::unique_ptr<tcp::socket> socket_;
    ConnectionState state_ = ConnectionState::disconnected;
};
