#pragma once

#include "net/transport.hpp"
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

class LocalTransportEndpoint : public ITransport {
  public:
    LocalTransportEndpoint();
    ~LocalTransportEndpoint();

    void set_peer(LocalTransportEndpoint *peer);

    void send(TransportMessage msg) override;
    void set_callback(Callback cb) override;
    void do_receive() override;
    bool is_connected() const override;

  private:
    Callback callback_;
    LocalTransportEndpoint *peer_ = nullptr;
    std::queue<TransportMessage> inbound_;
    std::mutex mutex_;
};

std::pair<std::unique_ptr<LocalTransportEndpoint>,
          std::unique_ptr<LocalTransportEndpoint>>
create_transport_pair();
