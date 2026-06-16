#include "UringCoroTcpServer.h"
#include "UringCoroTcpConnection.h"
#include "uring_poller.h"
#include "awaiter.h"
#include "../net/InetAddress.h"
#include <cassert>
#include <cstring>
#include "UringEventLoopThreadPool.h"
#include"../net/Socket.h"
#include <netinet/tcp.h>

namespace new_muduo {
namespace io_uring {
Task<void> UringTcpServer::start(uint16_t port) {

    listen_fd_ = new_muduo::createNonblockingOrDie();

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_KEEPALIVE, &opt, static_cast<socklen_t>(sizeof(opt)));
    ::setsockopt(listen_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, static_cast<socklen_t>(sizeof(opt)));


    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    int ret = ::bind(listen_fd_,
                     reinterpret_cast<struct sockaddr*>(&addr),
                     sizeof(addr));
    assert(ret == 0);

    ret = ::listen(listen_fd_, SOMAXCONN);
    assert(ret == 0);
    started_ = 1;
   

    threadPool_->start(threadInitCallback_);

    while (true) {
        UringContext ctx(loop_);
        sockaddr_in peer_addr;
        std::memset(&addr, 0, sizeof(addr));
        socklen_t peer_len;
        int conn_fd = co_await AcceptAwaiter{listen_fd_, peer_addr, peer_len, ctx };

        if (conn_fd < 0) {
            continue;
        }
        EventLoop* ioLoop = threadPool_->getNextLoop();
       
        onNewConnection(ioLoop,conn_fd,peer_addr);
    }

    co_return;
}


void UringTcpServer::onNewConnection(EventLoop* loop,int conn_fd, const sockaddr_in& peer_addr) {
    char buf[64];
    snprintf(buf, sizeof(buf), "#%d", nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;
    UringTcpConnectionPtr conn_ = std::make_shared<UringTcpConnection>(loop,conn_fd,&peer_addr,connName);
    connections_[connName] = conn_;
    conn_->setCloseCallback([this](const UringTcpConnectionPtr& c) {removeConnection(c); });
    conn_->setConnectionHandle(handle_);

   loop->runInLoop([conn_]() {
       conn_->start();
   });
}

void UringTcpServer::removeConnection(const UringTcpConnectionPtr& con)
{
    loop_->runInLoop(std::bind(&UringTcpServer::removeConnectionInLoop,this,con));
}

UringTcpServer::UringTcpServer(EventLoop* loop, const InetAddress& addr,const std::string& name):
        loop_(loop),
        name_(name),
        ipPort_(addr.toIpPort()),
        threadPool_(std::make_shared<UringEventLoopThreadPool>(loop, name)),
        started_(0),
        nextConnId_(1)
{}

UringTcpServer::~UringTcpServer()
{
    for (auto& [name_, conn_] : connections_) {
        UringTcpConnectionPtr guard(conn_);
        conn_.reset();
        guard->loop()->queueInLoop([guard]() {});
    }
}


void UringTcpServer::removeConnectionInLoop(const UringTcpConnectionPtr& conn) {
    connections_.erase(conn->name());

}

void UringTcpServer::setThreadNums(int numThreads) {
    assert(numThreads >= 0);
    threadPool_->setThreadNum(numThreads);
}



}
} 
