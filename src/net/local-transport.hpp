#pragma once

#include "net/transport.hpp"
#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <utility>

class LocalTransportEndpoint : public ITransport {
  public:
    LocalTransportEndpoint();
    ~LocalTransportEndpoint();

    void set_peer(LocalTransportEndpoint *peer);

    awaitable<void> write(TransportMessage msg) override;
    awaitable<TransportMessage> read() override;
    bool is_connected() const override;

  private:
    LocalTransportEndpoint *peer_ = nullptr;
    asio::experimental::channel<void(boost::system::error_code,
                                     TransportMessage)>
        channel_;
};

std::pair<std::unique_ptr<LocalTransportEndpoint>,
          std::unique_ptr<LocalTransportEndpoint>>
create_transport_pair();
