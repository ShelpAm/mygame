#pragma once

#include "net/transport.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class NetworkTransport : public ITransport {
  public:
    class Acceptor {
      public:
        explicit Acceptor(std::uint16_t port);
        Acceptor(Acceptor &&) noexcept = default;
        Acceptor &operator=(Acceptor &&) noexcept = default;
        ~Acceptor() = default;

        awaitable<std::unique_ptr<ITransport>> accept();
        void stop();

      private:
        std::shared_ptr<tcp::acceptor> impl_{};
    };

    NetworkTransport() = default;
    explicit NetworkTransport(tcp::socket sock);
    ~NetworkTransport()
    {
        socket_.close();
    }

    static awaitable<std::unique_ptr<NetworkTransport>>
    connect(std::string const &ip, int port);

    awaitable<void> write(TransportMessage msg) override;
    awaitable<TransportMessage> read() override;
    bool is_connected() const override;

    tcp::socket &socket()
    {
        return socket_;
    }

  private:
    void on_connected();
    void start_read();

    tcp::socket socket_{io()};
    std::atomic<bool> connected_{false};

    std::vector<std::uint8_t> write_buffer_;
    NetHead read_head_buffer_;
    std::vector<std::uint8_t> read_body_buffer_;
    std::mutex mutex_;
    std::vector<TransportMessage> recv_queue_;
};
