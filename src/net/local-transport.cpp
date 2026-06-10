#include "net/local-transport.hpp"

LocalTransportEndpoint::LocalTransportEndpoint() = default;

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

void LocalTransportEndpoint::send(TransportMessage msg)
{
    if (!peer_)
        return;
    std::lock_guard<std::mutex> lock(peer_->mutex_);
    peer_->inbound_.push(std::move(msg));
}

void LocalTransportEndpoint::set_callback(Callback cb)
{
    callback_ = std::move(cb);
}

void LocalTransportEndpoint::update()
{
    std::lock_guard<std::mutex> lock(mutex_);
    while (!inbound_.empty()) {
        auto msg = std::move(inbound_.front());
        inbound_.pop();
        if (callback_)
            callback_(msg);
    }
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
