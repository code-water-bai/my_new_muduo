#pragma once

#include "../base/noncopyable.h"
#include "../net/InetAddress.h"
#include "../net/Buffer.h"
#include "../net/Callbacks.h"
#include "Task.h"
#include "CoroChannel.h"

#include <memory>
#include <string>
#include <coroutine>

#define neo new_muduo

namespace neo {
    class Socket;
}  // namespace neo

namespace neo::coro {

    class CoroEventLoop;


    class CoroTcpConnection : noncopyable,
        public std::enable_shared_from_this<CoroTcpConnection> {
    public:
        using ConnectionHandler = std::function<Task<void> (std::shared_ptr<CoroTcpConnection>) >;

        CoroTcpConnection(CoroEventLoop* loop,
            int sockfd,
            const neo::InetAddress& localAddr,
            const neo::InetAddress& peerAddr,
            const std::string& name);
        ~CoroTcpConnection();

        const std::string& name() const { return name_; }
        const neo::InetAddress& localAddress() const { return localAddr_; }
        const neo::InetAddress& peerAddress() const { return peerAddr_; }
        int fd() const { return sockfd_; }
        bool connected() const { return connected_; }

        void setConnectionHandler(ConnectionHandler handler) { handler_ = std::move(handler); }
        void setCloseCallback(std::function<void(const std::shared_ptr<CoroTcpConnection>&)> cb) {
            closeCallback_ = std::move(cb);
        }
        void setDataCallback(std::function<void(std::shared_ptr<CoroTcpConnection>, neo::Buffer*)> cb) {
            dataCallback_ = std::move(cb);
        }

        // 协程入口：在 CoroEventLoop 中启动
        void start();

        // 发送数据
        void send(const std::string& data);
        void send(const void* data, size_t len);

        // 关闭连接
        void shutdown();

        neo::Buffer* inputBuffer() { return &inputBuffer_; }

    private:
        void onReadable();
        void onWritable();
        void handleClose();

        Task<void> connectionLoop();

        CoroEventLoop* loop_;
        int sockfd_;
        neo::InetAddress localAddr_;
        neo::InetAddress peerAddr_;
        std::string name_;
        bool connected_;
        bool writing_;

        std::unique_ptr<neo::Socket> socket_;
        std::unique_ptr<CoroChannel> channel_;
        neo::Buffer inputBuffer_;
        neo::Buffer outputBuffer_;

        ConnectionHandler handler_;
        std::function<void(std::shared_ptr<CoroTcpConnection>, neo::Buffer*)> dataCallback_;
        std::function<void(const std::shared_ptr<CoroTcpConnection>&)> closeCallback_;

        std::coroutine_handle<> writeWaiter_;
        Task<void> connectionTask_;  // 持有 connectionLoop 协程，保持连接存活
    };

    using CoroTcpConnectionPtr = std::shared_ptr<CoroTcpConnection>;

}  // namespace neo::coro