#include <functional>
#include <memory>
#include "../base/Timestamp.h"

#ifndef MUDUO_NET_CALLBACKS_H
#define MUDUO_NET_CALLBACKS_H
namespace new_muduo {

    class Buffer;
    class TcpConnection;
    class InetAddress;

    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;

    using TimerCallback = std::function<void()>;
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress& peerAddr)>;
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using Functor = std::function<void()>;
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
        Buffer*,
        Timestamp)>;
    using  WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using HighWaterMarkCallback = std::function<void(const std::shared_ptr<TcpConnection>&, size_t)>;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using ErrorCallback = std::function<void()>;
}

#endif

