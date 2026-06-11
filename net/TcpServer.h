#pragma once

#include "../base/noncopyable.h"
#include "Callbacks.h"
#include "Acceptor.h"
#include "InetAddress.h"

#include <map>
#include <memory>
#include <string>
#include <atomic>


namespace new_muduo {
	class EventLoop;
    namespace detail {
        class EventLoopThreadPool;
    }

    class TcpServer : noncopyable {
        
    public:
        using ThreadInitCallback = std::function<void(EventLoop*)>;
        using EventLoopThreadPool = new_muduo::detail::EventLoopThreadPool;

        TcpServer(EventLoop* loop,
            const InetAddress& listenAddr,
            const std::string& name,
            bool reusePort = true);
        ~TcpServer();

        const std::string& ipPort() const { return ipPort_; }
        const std::string& name() const { return name_; }
        EventLoop* getLoop() const { return loop_; }

        void setThreadNum(int numThreads);
        void setThreadInitCallback(const ThreadInitCallback& cb) { threadInitCallback_ = cb; }

        void start();

        void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
        void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
        void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }

    private:
        void newConnection(int sockfd, const InetAddress& peerAddr);
        void removeConnection(const TcpConnectionPtr& conn);
        void removeConnectionInLoop(const TcpConnectionPtr& conn);

        using ConnectionMap = std::map<std::string, TcpConnectionPtr>;

        EventLoop* loop_;
        const std::string ipPort_;
        const std::string name_;
        std::unique_ptr<Acceptor> acceptor_;
        std::shared_ptr<EventLoopThreadPool> threadPool_;
        ConnectionCallback connectionCallback_;
        MessageCallback messageCallback_;
        WriteCompleteCallback writeCompleteCallback_;
        ThreadInitCallback threadInitCallback_;
        std::atomic<int> started_;
        int nextConnId_;
        ConnectionMap connections_;
    };
}