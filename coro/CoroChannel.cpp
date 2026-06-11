#include "CoroChannel.h"
#include "CoroEventLoop.h"
#include "../net/Buffer.h"
#include "../base/Logging.h"
#include <functional>
#include <algorithm>
#include <utility>

namespace new_muduo::coro {

    CoroChannel::CoroChannel(CoroEventLoop* loop, int fd)
        : loop_(loop),
        fd_(fd),
        reading_(false),
        writing_(false),
        attached_(false) {
    }

    CoroChannel::~CoroChannel() {
        remove();
    }

    void CoroChannel::enableReading() {
        if (!attached_) {
            EventLoop* rawLoop = loop_->getRawLoop();
            channel_ = std::make_unique<neo::Channel>(rawLoop, fd_);
            channel_->setReadCallback([this](neo::Timestamp t) { handleRead(t); });
            channel_->setWriteCallback([this]() { handleWrite(); });
            channel_->setCloseCallback([this]() { handleClose(); });
            attached_ = true;
        }
        reading_ = true;
        channel_->enableReading();
    }

    void CoroChannel::disableReading() {
        reading_ = false;
        if (channel_) {
            channel_->disableReading();
        }
    }

    void CoroChannel::enableWriting() {
        if (!attached_) {
            auto* rawLoop = loop_->getRawLoop();
            channel_ = std::make_unique<neo::Channel>(rawLoop, fd_);
            channel_->setReadCallback([this](neo::Timestamp t) { handleRead(t); });
            channel_->setWriteCallback([this]() { handleWrite(); });
            channel_->setCloseCallback([this]() { handleClose(); });
            attached_ = true;
        }
        writing_ = true;
        channel_->enableWriting();
    }

    void CoroChannel::disableWriting() {
        writing_ = false;
        if (channel_) {
            channel_->disableWriting();
        }
    }

    void CoroChannel::disableAll() {
        reading_ = false;
        writing_ = false;
        if (channel_) {
            channel_->disableAll();
        }
    }

    void CoroChannel::remove() {
        disableAll();
        if (channel_) {
            channel_->remove();
            channel_.reset();
            attached_ = false;
        }
    }

    void CoroChannel::waitReadable(std::coroutine_handle<> h) {
        readWaiter_ = h;
        enableReading();
    }

    void CoroChannel::waitWritable(std::coroutine_handle<> h) {
        writeWaiter_ = h;
        enableWriting();
    }

    void CoroChannel::handleRead(neo::Timestamp t) {
        if (readWaiter_) {
            auto h = std::exchange(readWaiter_, nullptr);
            disableReading();
            loop_->scheduleResume(h);
        }
        else if (readCallback_) {
            // 非协程回调模式
            neo::Buffer buf;
            readCallback_(&buf);
        }
    }

    void CoroChannel::handleWrite() {
        if (writeWaiter_) {
            auto h = std::exchange(writeWaiter_, nullptr);
            disableWriting();
            loop_->scheduleResume(h);
        }
        else if (writeCallback_) {
            writeCallback_();
        }
    }

    void CoroChannel::handleClose() {
        if (readWaiter_) {
            auto h = std::exchange(readWaiter_, nullptr);
            loop_->scheduleResume(h);
        }
        if (closeCallback_) {
            closeCallback_();
        }
    }

}  // namespace neo::coro