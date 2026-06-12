#include "net/local-transport.hpp"
#include <spdlog/spdlog.h>

LocalTransportEndpoint::LocalTransportEndpoint()
    : channel_(ITransport::io(), 32)
{
}

LocalTransportEndpoint::~LocalTransportEndpoint()
{
    if (peer_) {
        peer_->peer_ = nullptr;
        peer_ = nullptr;
    }
}

void LocalTransportEndpoint::set_peer(LocalTransportEndpoint *peer)
{
    peer_ = peer;
}

awaitable<void> LocalTransportEndpoint::write(TransportMessage msg)
{
    assert(peer_);
    spdlog::debug("LocalTransportEndpoint ({}) writing message of type {} with "
                  "payload size {}",
                  (void *)this, static_cast<int>(msg.type), msg.payload.size());
    co_await peer_->channel_.async_send(boost::system::error_code(),
                                        TransportMessage{msg.type, msg.payload},
                                        use_awaitable);
}

awaitable<TransportMessage> LocalTransportEndpoint::read()
{
    co_return co_await channel_.async_receive(use_awaitable);
}

bool LocalTransportEndpoint::is_connected() const
{
    return peer_ != nullptr;
}

std::pair<std::unique_ptr<LocalTransportEndpoint>,
          std::unique_ptr<LocalTransportEndpoint>>
create_transport_pair()
{
    auto a = std::make_unique<LocalTransportEndpoint>();
    auto b = std::make_unique<LocalTransportEndpoint>();
    a->set_peer(b.get());
    b->set_peer(a.get());
    return {std::move(a), std::move(b)};
}
