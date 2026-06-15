#pragma once

#include "net/transport.hpp"
#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <utility>

class LocalTransportEndpoint
    : public ITransport,
      public std::enable_shared_from_this<LocalTransportEndpoint> {
  public:
    LocalTransportEndpoint();
    ~LocalTransportEndpoint();

    void set_peer(LocalTransportEndpoint *peer);

    awaitable<void> write(TransportMessage msg) override;
    awaitable<TransportMessage> read() override;
    bool is_open() const override;
    void close() override;
    std::string remote_info() const override;

  private:
    LocalTransportEndpoint *peer_ = nullptr;
    deferred_concurrent_channel<void(boost::system::error_code,
                                     TransportMessage)>
        channel_; // Read channel
};

std::pair<std::shared_ptr<LocalTransportEndpoint>,
          std::shared_ptr<LocalTransportEndpoint>>
create_transport_pair();
