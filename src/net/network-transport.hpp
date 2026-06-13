#pragma once

#include "net/transport.hpp"
#include <memory>
#include <mutex>
#include <vector>

using tcp_socket = default_token::as_default_on_t<tcp::socket>;
using tcp_acceptor = default_token::as_default_on_t<tcp::acceptor>;

class NetworkTransport : public ITransport {
  public:
    class Acceptor {
      public:
        explicit Acceptor(std::uint16_t port);
        Acceptor(Acceptor &&) noexcept = default;
        Acceptor &operator=(Acceptor &&) noexcept = default;
        ~Acceptor() = default;

        awaitable<std::unique_ptr<NetworkTransport>> accept();
        void stop();

      private:
        std::shared_ptr<tcp_acceptor> impl_{};
    };

    NetworkTransport() = default;
    explicit NetworkTransport(tcp::socket sock);
    ~NetworkTransport();

    static awaitable<std::unique_ptr<NetworkTransport>>
    connect(std::string const &ip, int port);

    awaitable<void> write(TransportMessage msg) override;
    awaitable<TransportMessage> read() override;
    bool is_connected() const override;
    void disconnect() override;

    tcp_socket &socket();

  private:
    tcp_socket socket_{io()};

    std::vector<std::uint8_t> write_buffer_;
    NetHead read_head_buffer_;
    std::vector<std::uint8_t> read_body_buffer_;
    std::mutex mutex_;
    std::vector<TransportMessage> recv_queue_;
};
