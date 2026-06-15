#pragma once

#include "net/transport.hpp"
#include <memory>
#include <vector>

using tcp_socket = default_token::as_default_on_t<tcp::socket>;
using tcp_acceptor = default_token::as_default_on_t<tcp::acceptor>;

class NetworkTransport : public ITransport,
                         public std::enable_shared_from_this<NetworkTransport> {
  public:
    class Acceptor : public std::enable_shared_from_this<Acceptor> {
      public:
        explicit Acceptor(std::uint16_t port);
        Acceptor(Acceptor &&) noexcept = default;
        Acceptor &operator=(Acceptor &&) noexcept = default;
        ~Acceptor() = default;

        awaitable<std::shared_ptr<NetworkTransport>> accept();
        void stop();

      private:
        std::shared_ptr<tcp_acceptor> impl_{};
    };

    NetworkTransport() = default;
    explicit NetworkTransport(tcp::socket sock);
    ~NetworkTransport();

    static awaitable<std::shared_ptr<NetworkTransport>>
    connect(std::string const &ip, int port);

    awaitable<void> write(TransportMessage msg) override;
    awaitable<TransportMessage> read() override;
    bool is_open() const override;
    void close() override;
    std::string remote_info() const override;

    tcp_socket &socket();

  private:
    tcp_socket socket_{io()};
    std::string cached_socket_info_;

    std::vector<std::uint8_t> write_buffer_;
    NetHead read_head_buffer_;
    std::vector<std::uint8_t> read_body_buffer_;
};
