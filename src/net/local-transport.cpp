#include "net/local-transport.hpp"
#include <spdlog/spdlog.h>

LocalTransportEndpoint::LocalTransportEndpoint()
    : channel_(ITransport::io(), 128)
{
}

LocalTransportEndpoint::~LocalTransportEndpoint()
{
    close();
}

void LocalTransportEndpoint::set_peer(LocalTransportEndpoint *peer)
{
    peer_ = peer;
}

awaitable<void> LocalTransportEndpoint::write(TransportMessage msg)
{
    if (!peer_)
        throw std::runtime_error(
            "LocalTransportEndpoint: write to disconnected peer " +
            remote_info());

    co_await peer_->channel_.async_send(boost::system::error_code(),
                                        {msg.type, msg.payload});
}

awaitable<TransportMessage> LocalTransportEndpoint::read()
{
    co_return co_await channel_.async_receive();
}

bool LocalTransportEndpoint::is_open() const
{
    return peer_ != nullptr;
}

void LocalTransportEndpoint::close()
{
    if (peer_) {
        if (peer_->channel_.is_open()) {
            peer_->channel_.cancel();
            peer_->channel_.close(); // cancel peer's pending reads
        }
        peer_->peer_ = nullptr;
        peer_ = nullptr;
    }
    if (channel_.is_open()) {
        channel_.cancel();
        channel_.close(); // cancel our pending reads
    }
}

std::pair<std::shared_ptr<LocalTransportEndpoint>,
          std::shared_ptr<LocalTransportEndpoint>>
create_transport_pair()
{
    auto a = std::make_shared<LocalTransportEndpoint>();
    auto b = std::make_shared<LocalTransportEndpoint>();
    a->set_peer(b.get());
    b->set_peer(a.get());
    return {std::move(a), std::move(b)};
}
std::string LocalTransportEndpoint::remote_info() const
{
    return "local (" + std::format("{}", static_cast<void const *>(this)) + ")";
}
