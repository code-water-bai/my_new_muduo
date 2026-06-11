#include "CoroTcpConnection.h"
#include "CoroEventLoop.h"
#include "../net/Socket.h"
#include "../base/Logging.h"

#include <unistd.h>
#include <cstring>
#define neo new_muduo

namespace neo::coro {

    CoroTcpConnection::CoroTcpConnection(CoroEventLoop* loop,
        int sockfd,
        const neo::InetAddress& localAddr,
        const neo::InetAddress& peerAddr,
        const std::string& name)
        : loop_(loop),
        sockfd_(sockfd),
        localAddr_(localAddr),
        peerAddr_(peerAddr),
        name_(name),
        connected_(false),
        writing_(false),
        socket_(std::make_unique<neo::Socket>(sockfd)),
        channel_(std::make_unique<CoroChannel>(loop, sockfd)) {
        socket_->setTcpNoDelay(true);
        socket_->setKeepAlive(true);
    }

    CoroTcpConnection::~CoroTcpConnection() {
        channel_->remove();
    }

    void CoroTcpConnection::start() {
        connected_ = true;

        channel_->setReadCallback([this](neo::Buffer*) { onReadable(); });
        channel_->setCloseCallback([this]() { handleClose(); });
        channel_->setWriteCallback([this]() {onWritable(); });
        channel_->enableReading();

        // 启动协程并持有 Task
        connectionTask_ = connectionLoop();
        if (connectionTask_) {
            connectionTask_.handle().resume();
        }
    }

    Task<void> CoroTcpConnection::connectionLoop() {
        if (handler_) {
            co_await handler_(shared_from_this());
        }
        // 连接关闭
        connected_ = false;
        if (closeCallback_) {
            closeCallback_(shared_from_this());
        }
    }

    void CoroTcpConnection::onReadable() {
        char buf[65536];
        ssize_t n = ::read(sockfd_, buf, sizeof(buf));
        if (n > 0) {
            inputBuffer_.append(buf, n);
            if (dataCallback_) {
                dataCallback_(shared_from_this(), &inputBuffer_);
            }
        }
        else if (n == 0) {
            handleClose();
        }
    }

    void CoroTcpConnection::onWritable() {
        if (!outputBuffer_.readableBytes()) {
            channel_->disableWriting();
            writing_ = false;
        }
        else {
            ssize_t n = ::write(sockfd_,
                outputBuffer_.peek(),
                outputBuffer_.readableBytes());
            if (n > 0) {
                outputBuffer_.retrieve(n);
                if (!outputBuffer_.readableBytes()) {
                    channel_->disableWriting();
                    writing_ = false;
                }
            }
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                handleClose();
            }
        }
    }

    void CoroTcpConnection::send(const std::string& data) {
        send(data.data(), data.size());
    }

    void CoroTcpConnection::send(const void* data, size_t len) {
        if (!connected_) return;

        outputBuffer_.append(data, len);
        if (!writing_) {
            writing_ = true;
            channel_->enableWriting();
        }
    }

    void CoroTcpConnection::shutdown() {
        connected_ = false;
        if (socket_) {
            socket_->shutdownWrite();
        }
    }

    void CoroTcpConnection::handleClose() {
        if (connected_) {
            connected_ = false;
            channel_->disableAll();
            channel_->remove();
            if (closeCallback_) {
                closeCallback_(shared_from_this());
            }
        }
    }

}  // namespace neo::coro