#pragma once

#include "net/session.hpp"
#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <utility>

class LocalSession : public Session,
                     public std::enable_shared_from_this<LocalSession> {
  public:
    LocalSession();
    ~LocalSession();

    void set_peer(std::weak_ptr<LocalSession> peer);

    awaitable<void> write(TransportMessage msg) override;
    awaitable<TransportMessage> read() override;
    bool is_open() const override;
    void close() override;
    std::string remote_info() const override;

  private:
    std::weak_ptr<LocalSession> peer_;
    deferred_concurrent_channel<void(boost::system::error_code,
                                     TransportMessage)>
        channel_; // Read channel
};

std::pair<std::shared_ptr<LocalSession>, std::shared_ptr<LocalSession>>
create_local_transport_pair();
