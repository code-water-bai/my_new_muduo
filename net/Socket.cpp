#include "Socket.h"
#include "InetAddress.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstring>
#include "../base/Logging.h"

namespace new_muduo{

    Socket::~Socket() {
        if (sockfd_ >= 0) {
            ::close(sockfd_);
        }
    }


    void Socket::bindAddress(const InetAddress& addr) {
        const struct sockaddr* saddr =(sockaddr*) addr.getSockAddr();
        socklen_t addrlen = sizeof(*addr.getSockAddr());
        int ret = ::bind(sockfd_, saddr, addrlen);
       /* if (ret < 0) {
            LOG_SYSFATAL << "Socket::bindAddress";
        }*/
    }

    void Socket::listen() {
        int ret = ::listen(sockfd_, SOMAXCONN);
        if (ret < 0) {
            LOG_SYSFATAL << "Socket::listen";
        }
    }

    int Socket::accept(InetAddress* peeraddr) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        socklen_t addrlen = static_cast<socklen_t>(sizeof(addr));
        int connfd = ::accept(sockfd_, reinterpret_cast<struct sockaddr*>(&addr),
            &addrlen);
        if (connfd >= 0) {
            peeraddr->setAddr(addr);
        }
        return connfd;
    }


    void Socket::shutdownWrite() {
        if (::shutdown(sockfd_, SHUT_WR) < 0) {
            LOG_SYSERR << "Socket::shutdownWrite";
        }
    }

    void Socket::setTcpNoDelay(bool on) { setTcpNoDelayStatic(sockfd_, on); }
    void Socket::setReuseAddr(bool on) { setReuseAddrStatic(sockfd_, on); }
    void Socket::setReusePort(bool on) { setReusePortStatic(sockfd_, on); }
    void Socket::setKeepAlive(bool on) { setKeepAliveStatic(sockfd_, on); }


    void Socket::setTcpNoDelayStatic(int fd, bool on) {
        int optval = on ? 1 : 0;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, static_cast<socklen_t>(sizeof(optval)));
    }

    void Socket::setReuseAddrStatic(int fd, bool on) {
        int optval = on ? 1 : 0;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, static_cast<socklen_t>(sizeof(optval)));
    }

    void Socket::setReusePortStatic(int fd, bool on) {
        int optval = on ? 1 : 0;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, static_cast<socklen_t>(sizeof(optval)));
    }

    void Socket::setKeepAliveStatic(int fd, bool on) {
        int optval = on ? 1 : 0;
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, static_cast<socklen_t>(sizeof(optval)));
    }

    int Socket::getSocketError() const {
        int optval;
        socklen_t optlen = static_cast<socklen_t>(sizeof(optval));
        if (::getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
            return errno;
        }
        return optval;
    }



        int createNonblockingOrDie() {
            int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
            if (sockfd < 0) {
                LOG_SYSFATAL << "sockets::createNonblockingOrDie";
            }
            return sockfd;
        }

        int createNonblockingUdpOrDie() {
            int sockfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
            if (sockfd < 0) {
                LOG_SYSFATAL << "sockets::createNonblockingUdpOrDie";
            }
            return sockfd;
        }

        void setNonBlockAndCloseOnExec(int sockfd) {
            int flags = ::fcntl(sockfd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            int ret = ::fcntl(sockfd, F_SETFL, flags);
            if (ret < 0) {
                LOG_SYSERR << "sockets::setNonBlockAndCloseOnExec";
            }

            flags = ::fcntl(sockfd, F_GETFD, 0);
            flags |= FD_CLOEXEC;
            ret = ::fcntl(sockfd, F_SETFD, flags);
            if (ret < 0) {
                LOG_SYSERR << "sockets::setNonBlockAndCloseOnExec";
            }
        }

        int connect(int sockfd, const InetAddress& addr) {
            auto* saddr = addr.getSockAddr();
            socklen_t addrlen = sizeof(*addr.getSockAddr());
            return ::connect(sockfd, (sockaddr*)saddr, addrlen);
        }

        bool isSelfConnect(int sockfd) {
             ::sockaddr_in localaddr = *getLocalAddr(sockfd).getSockAddr();
             ::sockaddr_in peeraddr = *getPeerAddr(sockfd).getSockAddr();
            if (localaddr.sin_family == AF_INET) {
                auto* laddr4 = reinterpret_cast<const   ::sockaddr_in*>(&localaddr);
                auto* paddr4 = reinterpret_cast<const   ::sockaddr_in*>(&peeraddr);
                return laddr4->sin_port == paddr4->sin_port &&
                    laddr4->sin_addr.s_addr == paddr4->sin_addr.s_addr;
            }
            return false;
        }

        InetAddress getLocalAddr(int sockfd) {
          ::sockaddr_in localaddr;
            memset(&localaddr, 0, sizeof(localaddr));
            socklen_t addrlen = static_cast<socklen_t>(sizeof(localaddr));
            if (::getsockname(sockfd, reinterpret_cast<struct sockaddr*>(&localaddr), &addrlen) < 0) {
                LOG_SYSERR << "sockets::getLocalAddr";
            }
            return   InetAddress(localaddr);
        }

        InetAddress getPeerAddr(int sockfd) {
             ::sockaddr_in peeraddr;
            memset(&peeraddr, 0, sizeof(peeraddr));
            socklen_t addrlen = static_cast<socklen_t>(sizeof(peeraddr));
            if (::getpeername(sockfd, reinterpret_cast<struct sockaddr*>(&peeraddr), &addrlen) < 0) {
                LOG_SYSERR << "sockets::getPeerAddr";
            }
            return InetAddress(peeraddr);
        }
    
}