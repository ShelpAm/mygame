// AI-generated content, use at your own risk.

#pragma once

#include "net/transport.hpp"
#include <memory>

class TransportGuard {
  public:
    // 构造时接管底层的 shared_ptr
    explicit TransportGuard(std::shared_ptr<ITransport> t)
        : transport_(std::move(t))
    {
    }

    // 核心魔法：析构函数中自动触发关闭
    ~TransportGuard()
    {
        if (transport_) {
            // 这里恢复了 RAII 的灵魂：对象销毁 = 资源(连接)断开
            transport_->close();
        }
    }

    // 禁用拷贝，允许移动 (标准的独占资源语义)
    TransportGuard(TransportGuard const &) = delete;
    TransportGuard &operator=(TransportGuard const &) = delete;
    TransportGuard(TransportGuard &&) = default;
    TransportGuard &operator=(TransportGuard &&) = default;

    // 提供一个获取底层指针的方法，方便拿去发消息
    ITransport *get() const
    {
        return transport_.get();
    }

  private:
    std::shared_ptr<ITransport> transport_;
};
