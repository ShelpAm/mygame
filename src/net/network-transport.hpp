#pragma once

#include "net/transport.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <vector>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;

class NetworkTransport : public ITransport {
  public:
    class Acceptor {
      public:
        Acceptor(std::uint16_t port);
        Acceptor(Acceptor &&) noexcept = default;
        Acceptor &operator=(Acceptor &&) noexcept = default;
        ~Acceptor() = default;

        awaitable<std::unique_ptr<ITransport>> accept();
        void stop();

      private:
        std::shared_ptr<tcp::acceptor> impl_{};
    };

    static awaitable<std::unique_ptr<NetworkTransport>>
    connect(std::string const &ip, int port);
    static void shutdown();

    void send(TransportMessage msg) override;
    void set_callback(Callback cb) override;
    void consume() override;
    bool is_connected() const override;

    tcp::socket &socket()
    {
        return socket_;
    }
    void on_connected();

    static boost::asio::io_context &io();

  private:
    void start_read();

    tcp::socket socket_{io()};
    Callback callback_;
    std::atomic<bool> connected_{false};

    std::mutex mutex_;
    std::vector<TransportMessage> recv_queue_;
};
