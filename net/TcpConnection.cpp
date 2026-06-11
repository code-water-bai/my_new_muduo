#include "TcpConnection.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "../base/Logging.h"

#include <unistd.h>

namespace new_muduo {

    void defaultConnectionCallback(const TcpConnectionPtr& conn) {
        LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
    }

    void defaultMessageCallback(const TcpConnectionPtr&, Buffer* buf, Timestamp) {
        buf->retrieveAll();
    }

    TcpConnection::TcpConnection(EventLoop* loop,
        const std::string& name,
        int sockfd,
        const InetAddress& localAddr,
        const InetAddress& peerAddr)
        : loop_(loop),
        name_(name),
        state_(kConnecting),
        reading_(true),
        socket_(std::make_unique<Socket>(sockfd)),
        channel_(std::make_unique<Channel>(loop, sockfd)),
        localAddr_(localAddr),
        peerAddr_(peerAddr),
        highWaterMark_(64 * 1024 * 1024) {
        channel_->setReadCallback([this](Timestamp t) { handleRead(t); });
        channel_->setWriteCallback([this]() { handleWrite(); });
        channel_->setCloseCallback([this]() { handleClose(); });
        channel_->setErrorCallback([this]() { handleError(); });
        LOG_DEBUG << "TcpConnection::ctor[" << name_ << "] at " << this
            << " fd=" << sockfd;
        socket_->setKeepAlive(true);
    }

    TcpConnection::~TcpConnection() {
        LOG_DEBUG << "TcpConnection::dtor[" << name_ << "] at " << this
            << " fd=" << channel_->fd()
            << " state=" << state_;
        assert(state_ == kDisconnected);
    }

    void TcpConnection::send(const void* data, int len) {
        send(std::string(static_cast<const char*>(data), len));
    }

    void TcpConnection::send(const std::string& message) {
        if (state_ == kConnected) {
            if (loop_->isInLoopThread()) {
                sendInLoop(message);
            }
            else {
                loop_->runInLoop([this, message]() { sendInLoop(message); });
            }
        }
    }

    void TcpConnection::send(Buffer* buf) {
        if (state_ == kConnected) {
            if (loop_->isInLoopThread()) {
                sendInLoop(buf->peek(), buf->readableBytes());
                buf->retrieveAll();
            }
            else {
                loop_->runInLoop([this, buf]() {
                    sendInLoop(buf->peek(), buf->readableBytes());
                    buf->retrieveAll();
                    });
            }
        }
    }

    void TcpConnection::sendInLoop(const void* data, size_t len) {
        loop_->assertInLoopThread();
        ssize_t nwrote = 0;
        size_t remaining = len;
        bool faultError = false;

        if (state_ == kDisconnected) {
            LOG_WARN << "disconnected, give up writing";
            return;
        }

        if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
            nwrote = ::write(channel_->fd(), data, len);
            if (nwrote >= 0) {
                remaining = len - nwrote;
                if (remaining == 0 && writeCompleteCallback_) {
                    loop_->queueInLoop([shared_this = shared_from_this()]() {
                        shared_this->writeCompleteCallback_(shared_this);
                        });
                }
            }
            else {
                nwrote = 0;
                if (errno != EWOULDBLOCK) {
                    LOG_SYSERR << "TcpConnection::sendInLoop";
                    if (errno == EPIPE || errno == ECONNRESET) {
                        faultError = true;
                    }
                }
            }
        }

        assert(remaining <= len);
        if (!faultError && remaining > 0) {
            size_t oldLen = outputBuffer_.readableBytes();
            if (oldLen + remaining >= highWaterMark_ &&
                oldLen < highWaterMark_ &&
                highWaterMarkCallback_) {
                loop_->queueInLoop([shared_this = shared_from_this(), oldLen, remaining]() {
                    shared_this->highWaterMarkCallback_(shared_this, oldLen + remaining);
                    });
            }
            outputBuffer_.append(static_cast<const char*>(data) + nwrote, remaining);
            if (!channel_->isWriting()) {
                channel_->enableWriting();
            }
        }
    }

    void TcpConnection::sendInLoop(const std::string& message) {
        sendInLoop(message.data(), message.size());
    }

    void TcpConnection::shutdown() {
        if (state_ == kConnected) {
            setState(kDisconnecting);
            loop_->runInLoop([this]() { shutdownInLoop(); });
        }
    }

    void TcpConnection::shutdownInLoop() {
        loop_->assertInLoopThread();
        if (!channel_->isWriting()) {
            socket_->shutdownWrite();
        }
    }

    void TcpConnection::forceClose() {
        if (state_ == kConnected || state_ == kDisconnecting) {
            setState(kDisconnecting);
            loop_->queueInLoop([shared_this = shared_from_this()]() {
                shared_this->forceCloseInLoop();
                });
        }
    }

    void TcpConnection::forceCloseWithDelay(double seconds) {
        if (state_ == kConnected || state_ == kDisconnecting) {
            setState(kDisconnecting);
            loop_->runAfter(seconds, [shared_this = shared_from_this()]() {
                shared_this->forceCloseInLoop();
                });
        }
    }

    void TcpConnection::forceCloseInLoop() {
        loop_->assertInLoopThread();
        if (state_ == kConnected || state_ == kDisconnecting) {
            handleClose();
        }
    }

    void TcpConnection::setTcpNoDelay(bool on) {
        socket_->setTcpNoDelay(on);
    }

    void TcpConnection::startRead() {
        loop_->runInLoop([this]() {
            if (!reading_ || !channel_->isReading()) {
                channel_->enableReading();
                reading_ = true;
            }
            });
    }

    void TcpConnection::stopRead() {
        loop_->runInLoop([this]() {
            if (reading_ || channel_->isReading()) {
                channel_->disableReading();
                reading_ = false;
            }
            });
    }

    void TcpConnection::connectEstablished() {
        assert(state_ == kConnecting);
        setState(kConnected);
        channel_->tie(shared_from_this());
        channel_->enableReading();

        connectionCallback_(shared_from_this());
    }

    void TcpConnection::connectDestroyed() {
        if (state_ == kConnected) {
            setState(kDisconnected);
            channel_->disableAll();
            connectionCallback_(shared_from_this());
        }
        channel_->remove();
    }

    void TcpConnection::handleRead(Timestamp receiveTime) {
        int savedErrno = 0;
        ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
        if (n > 0) {
            messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
        }
        else if (n == 0) {
            handleClose();
        }
        else {
            errno = savedErrno;
            LOG_SYSERR << "TcpConnection::handleRead";
            handleError();
        }
    }

    void TcpConnection::handleWrite() {
        if (channel_->isWriting()) {
            ssize_t n = ::write(channel_->fd(),
                outputBuffer_.peek(),
                outputBuffer_.readableBytes());
            if (n > 0) {
                outputBuffer_.retrieve(n);
                if (outputBuffer_.readableBytes() == 0) {
                    channel_->disableWriting();
                    if (writeCompleteCallback_) {
                        loop_->queueInLoop([shared_this = shared_from_this()]() {
                            shared_this->writeCompleteCallback_(shared_this);
                            });
                    }
                    if (state_ == kDisconnecting) {
                        shutdownInLoop();
                    }
                }
            }
            else {
                LOG_SYSERR << "TcpConnection::handleWrite";
            }
        }
        else {
            LOG_TRACE << "Connection fd = " << channel_->fd()
                << " is down, no more writing";
        }
    }

    void TcpConnection::handleClose() {
        LOG_TRACE << "fd = " << channel_->fd() << " state = " << state_;
        assert(state_ == kConnected || state_ == kDisconnecting);
        setState(kDisconnected);
        channel_->disableAll();

        TcpConnectionPtr guardThis(shared_from_this());
        connectionCallback_(guardThis);
        closeCallback_(guardThis);
    }

    void TcpConnection::handleError() {
        int err = socket_.get()->getSocketError();
        LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err;
    }

}  // namespace neo