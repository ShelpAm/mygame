#pragma once

#include "net/transport.hpp"

class NetworkManager;

class NetworkTransport : public ITransport {
  public:
    explicit NetworkTransport(NetworkManager &net);

    void send(TransportMessage msg) override;
    void set_callback(Callback cb) override;
    void update() override;
    bool is_connected() const override;

  private:
    NetworkManager &net_;
    Callback callback_;
};
