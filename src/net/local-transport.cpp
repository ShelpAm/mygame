#include "net/local-transport.hpp"
#include <spdlog/spdlog.h>

LocalTransportEndpoint::LocalTransportEndpoint()
    : channel_(ITransport::io(), 128)
{
}

LocalTransportEndpoint::~LocalTransportEndpoint()
{
    disconnect();
}

void LocalTransportEndpoint::set_peer(LocalTransportEndpoint *peer)
{
    peer_ = peer;
}

awaitable<void> LocalTransportEndpoint::write(TransportMessage msg)
{
    if (!peer_)
        throw std::runtime_error(
            "LocalTransportEndpoint: write to disconnected peer");

    co_await peer_->channel_.async_send(boost::system::error_code(),
                                        {msg.type, msg.payload});
}

awaitable<TransportMessage> LocalTransportEndpoint::read()
{
    co_return co_await channel_.async_receive();
}

bool LocalTransportEndpoint::is_connected() const
{
    return peer_ != nullptr;
}

void LocalTransportEndpoint::disconnect()
{
    if (peer_) {
        peer_->peer_ = nullptr;
        peer_ = nullptr;
        channel_.close();
    }
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
