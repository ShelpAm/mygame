#include "net/local-session.hpp"
#include <spdlog/spdlog.h>

LocalSession::LocalSession() : channel_(Session::io(), 128)
{
}

LocalSession::~LocalSession()
{
    close();
}

void LocalSession::set_peer(std::weak_ptr<LocalSession> peer)
{
    peer_ = std::move(peer);
}

awaitable<void> LocalSession::write(TransportMessage msg)
{
    auto p = peer_.lock();
    if (!p)
        throw std::runtime_error("LocalTransportEndpoint: write to disconnected peer " +
                                 remote_info());

    auto &c = p->channel_;
    if (!c.try_send(boost::system::error_code(), msg))
        co_await c.async_send(boost::system::error_code(), std::move(msg));
}

awaitable<TransportMessage> LocalSession::read()
{
    co_return co_await channel_.async_receive();
}

bool LocalSession::is_open() const
{
    return peer_.lock() != nullptr;
}

void LocalSession::close()
{
    // 1. 先关闭自己的接收通道。
    // 这会让所有目前卡在本地 `read()` (async_receive) 上的协程立刻醒来，
    // 收到 `channel_closed` 错误，从而安全、优雅地退出读循环。
    channel_.close();

    // 2. 通知对端：我不会再给你发数据了。
    // 在内存通道里，“通知对端”最好的方式不是强杀对端，而是解除绑定，或者让对端在写失败时自己感知。
    if (auto peer_ptr = peer_.lock()) {
        // 注意：这里绝对不要去 peer_ptr->channel_.close()！

        // 我们可以安全地解除双向引用，打破循环引用
        peer_.reset();

        // 让对端也知道我已经松手了（可选：可以通过特殊消息或者对端自行通过
        // weak_ptr 校验） peer_ptr->on_peer_closed();
        asio::post(io(), [peer_ptr]() { peer_ptr->close(); });
    }
}

std::pair<std::shared_ptr<LocalSession>, std::shared_ptr<LocalSession>>
create_local_transport_pair()
{
    auto a = std::make_shared<LocalSession>();
    auto b = std::make_shared<LocalSession>();
    a->set_peer(b);
    b->set_peer(a);
    return {std::move(a), std::move(b)};
}
std::string LocalSession::remote_info() const
{
    return "local (" + std::format("{}", static_cast<void const *>(this)) + ")";
}
