#ifndef MUDUO_URING_TCP_CONNECTION_H
#define MUDUO_URING_TCP_CONNECTION_H


#include "awaiter.h"

#include <functional>
#include <memory>
#include <string>
#include "../coro/Task.h"
#include "../net/InetAddress.h"


namespace new_muduo {
    class EventLoop;
    class InetAddress;
namespace io_uring {

class UringPoller;


class UringTcpConnection : public std::enable_shared_from_this<UringTcpConnection> {
public:
   enum State{KConnecting,KConnected,KDisconnecting,KDisconnected};
    using UringTcpConnectionPtr = std::shared_ptr< UringTcpConnection>;
    using CloseCallback    = std::function<void(const std::shared_ptr<UringTcpConnection>&)>;
    using ConnectionHandle = std::function<Task<void>(UringTcpConnectionPtr)>;
     UringTcpConnection(EventLoop* loop, int fd,const sockaddr_in* peer_addr,
                  const std::string& name = "TcpConnection");
    ~UringTcpConnection();

    UringTcpConnection(const UringTcpConnection&) = delete;
    UringTcpConnection& operator=(const UringTcpConnection&) = delete;

    void start();
    void shutdown();
    void forceclose();

    void setCloseCallback(CloseCallback cb)       { closeCallback_   = std::move(cb); }
    void setConnectionHandle(ConnectionHandle handle) { handle_ = handle; }

    //Task<ssize_t> readsome(char* buf);
    Task<ssize_t> read(char* buf, signed len);
    Task<ssize_t> send(const std::string& message);
    Task<ssize_t> send(const char* data, int len);


    int fd() const { return fd_; }
    const std::string& name() const { return name_; }
    EventLoop* loop() { return loop_; }
    const InetAddress& peerAddress() const { return peer_addr_; }
private:
    void forcecloseInLoop();
    void shutdownInLoop();
    void handleClose();
    Task<void> connectionLoop();
private:
    EventLoop* loop_;
    int           fd_;
    std::string   name_;
    InetAddress  peer_addr_;
    ConnectionHandle handle_;
    State state_;
    CloseCallback   closeCallback_;
    Task<void> connectionTask_;
};

} 
} 

#endif 
