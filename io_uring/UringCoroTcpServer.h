#ifndef MUDUO_URING_TCP_SERVER_H
#define MUDUO_URING_TCP_SERVER_H


#include <functional>
#include <memory>
#include <string>
#include <netinet/in.h>
#include "../coro/Task.h"
#include"../net/Callbacks.h"



namespace new_muduo {
   
    class EventLoop;
    class InetAddress;
namespace io_uring {
 class UringEventLoopThreadPool;
class UringPoller;
class UringTcpConnection;
class UringEventLoop;


class UringTcpServer {
public:
    using UringTcpConnectionPtr = std::shared_ptr< UringTcpConnection>;
    using ConnectionHandle = std::function<Task<void>(UringTcpConnectionPtr)>;
    using CloseCallback = std::function<void(UringTcpConnectionPtr)>;
    using ConnectionMap = std::unordered_map<std::string, UringTcpConnectionPtr>;
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    UringTcpServer(EventLoop* loop, const InetAddress& add_ , const std::string& name = "UringTcpServer");
    ~UringTcpServer();

    UringTcpServer(const UringTcpServer&) = delete;
    UringTcpServer& operator=(const UringTcpServer&) = delete;
    void setThreadNums(int nums);
    Task<void> start(uint16_t port);
    int listenFd() const { return listen_fd_; }
    void setConnectionHandle(ConnectionHandle h) { handle_ = std::move(h); }
private:
    void onNewConnection(EventLoop* loop,int conn_fd,const sockaddr_in& peer_addr);
    void removeConnection(const UringTcpConnectionPtr& con);
   void removeConnectionInLoop(const UringTcpConnectionPtr& con);

    EventLoop* loop_;
    const std::string ipPort_;
    const std::string name_;
    int listen_fd_;
    std::shared_ptr<UringEventLoopThreadPool> threadPool_;
    std::atomic<int> started_;
    int nextConnId_;
    ConnectionMap connections_;
    ThreadInitCallback threadInitCallback_;
    ConnectionHandle handle_;
};

} 
} 

#endif 
