#include "CoroTcpServer.h"
#include "CoroEventLoop.h"
#include "../net/Socket.h"
#include "../net/InetAddress.h"
#include "../base/Logging.h"
#include "CoroChannel.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>



namespace neo::coro {

    CoroTcpServer::CoroTcpServer(CoroEventLoop* loop,
        const neo::InetAddress& listenAddr,
        const std::string& name)
        : loop_(loop),
        listenAddr_(listenAddr),
        name_(name),
        acceptFd_(-1),
        started_(0),
        nextConnId_(1) {
    }

    CoroTcpServer::~CoroTcpServer() {
        if (acceptFd_ >= 0) {
            ::close(acceptFd_);
        }
    }

    void CoroTcpServer::start() {
        if (started_.fetch_add(1) > 0) return;

        acceptFd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
        if (acceptFd_ < 0) {
            LOG_SYSFATAL << "CoroTcpServer::start - socket creation failed";
        }

        int optval = 1;
        ::setsockopt(acceptFd_, SOL_SOCKET, SO_REUSEADDR, &optval, static_cast<socklen_t>(sizeof(optval)));

        const struct sockaddr* saddr = (sockaddr*)listenAddr_.getSockAddr();
        socklen_t addrlen = sizeof(*listenAddr_.getSockAddr());
        if (::bind(acceptFd_, saddr, addrlen) < 0) {
            LOG_SYSFATAL << "CoroTcpServer::start - bind failed";
        }
        if (::listen(acceptFd_, SOMAXCONN) < 0) {
            LOG_SYSFATAL << "CoroTcpServer::start - listen failed";
        }

        acceptChannel_ = std::make_unique<CoroChannel>(loop_, acceptFd_);
        acceptChannel_->setReadCallback([this](neo::Buffer*) { onAccept(); });
        acceptChannel_->enableReading();

        LOG_TRACE << "CoroTcpServer[" << name_ << "] listening on " << listenAddr_.toIpPort();
    }

    void CoroTcpServer::onAccept() {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        socklen_t addrlen = static_cast<socklen_t>(sizeof(addr));
        int connfd = ::accept4(acceptFd_, reinterpret_cast<struct sockaddr*>(&addr),
            &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connfd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_SYSERR << "CoroTcpServer::onAccept";
            }
            return;
        }

        neo::InetAddress peerAddr(addr);
        neo::InetAddress localAddr(getLocalAddr(connfd));

        char buf[64];
        snprintf(buf, sizeof(buf), "#%d", nextConnId_++);
        std::string connName = name_ + buf;

        auto conn = std::make_shared<CoroTcpConnection>(
            loop_, connfd, localAddr, peerAddr, connName);

        conn->setConnectionHandler(handler_);
        conn->setDataCallback(dataCallback_);
        conn->setCloseCallback([this](const std::shared_ptr<CoroTcpConnection>& c) {
            removeConnection(c);
        });

        connections_[connName] = conn;

        LOG_TRACE << "CoroTcpServer[" << name_ << "] new connection from " << peerAddr.toIpPort();

        conn->start();
    }

    void CoroTcpServer::removeConnection(const std::shared_ptr<CoroTcpConnection>& conn) {
        connections_.erase(conn->name());
    }

}  // namespace neo::coro