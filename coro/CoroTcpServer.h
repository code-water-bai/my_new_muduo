#pragma once

#include "../base/noncopyable.h"
#include "../net/InetAddress.h"
#include "CoroTcpConnection.h"

#include <map>
#include <memory>
#include <string>
#include <atomic>
#define neo new_muduo

namespace neo {
    class EventLoop;
}  // namespace neo

namespace neo::coro {

    class CoroEventLoop;
    class CoroTcpConnection;
    class CoroChannel;

   
    class CoroTcpServer : noncopyable {
    public:
        using ConnectionHandler = CoroTcpConnection::ConnectionHandler;
        using DataCallback = std::function<void(std::shared_ptr<CoroTcpConnection>, Buffer*)>;

        CoroTcpServer(CoroEventLoop* loop,
            const neo::InetAddress& listenAddr,
            const std::string& name);
        ~CoroTcpServer();

        void setConnectionHandler(ConnectionHandler handler) { handler_ = std::move(handler); }
        void setDataCallback(DataCallback cb) { dataCallback_ = std::move(cb); }
        void start();

        const std::string& name() const { return name_; }

    private:
        void onAccept();
        void removeConnection(const std::shared_ptr<CoroTcpConnection>& conn);

        using ConnectionMap = std::map<std::string, std::shared_ptr<CoroTcpConnection>>;

        CoroEventLoop* loop_;
        neo::InetAddress listenAddr_;
        std::string name_;
        int acceptFd_;
        ConnectionHandler handler_;
        DataCallback dataCallback_;
        std::unique_ptr<CoroChannel> acceptChannel_;
        ConnectionMap connections_;
        std::atomic<int> started_;
        int nextConnId_;
    };

}  // namespace neo::coro