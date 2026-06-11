#include "Acceptor.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "../base/Logging.h"

#include <fcntl.h>
#include <unistd.h>


namespace new_muduo {
    Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
        : loop_(loop),
        acceptSocket_(createNonblockingOrDie()),
        acceptChannel_(loop, acceptSocket_.fd()),
        listening_(false),
        idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) {
        acceptSocket_.setReuseAddr(true);
        acceptSocket_.setReusePort(reuseport);
        acceptSocket_.bindAddress(listenAddr);
        acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
    }

    Acceptor::~Acceptor() {
        acceptChannel_.disableAll();
        acceptChannel_.remove();
        ::close(idleFd_);
    }

    void Acceptor::listen() {
        listening_ = true;
        acceptSocket_.listen();
        acceptChannel_.enableReading();
    }

    void Acceptor::handleRead() {
        InetAddress peerAddr;
        int connfd = acceptSocket_.accept(&peerAddr);
        if (connfd >= 0) {
            if (newConnectionCallback_) {
                newConnectionCallback_(connfd, peerAddr);
            }
            else {
                ::close(connfd);
            }
        }
        else {
            LOG_SYSERR << "in Acceptor::handleRead";
            if (errno == EMFILE) {
                ::close(idleFd_);
                idleFd_ = ::accept(acceptSocket_.fd(), nullptr, nullptr);
                ::close(idleFd_);
                idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
            }
        }
    }

}