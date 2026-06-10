#include "net/network-transport.hpp"

#include "net/net-packet.hpp"
#include "net/network-manager.hpp"

NetworkTransport::NetworkTransport(NetworkManager &net) : net_(net) {}

void NetworkTransport::send(TransportMessage msg)
{
    auto data = serialize_packet({msg.type, std::move(msg.payload)});
    net_.queue_send(std::move(data));
}

void NetworkTransport::set_callback(Callback cb)
{
    callback_ = std::move(cb);
    net_.set_callback([this](NetPacket const &pkt) {
        if (callback_)
            callback_({this, pkt.type, pkt.payload});
    });
}

void NetworkTransport::do_receive()
{
    net_.update();
}

bool NetworkTransport::is_connected() const
{
    return net_.is_connected();
}
