#include "Channel.h"
#include "EventLoop.h"
#include "../base/Logging.h"

#include <sys/epoll.h>
#include <sstream>
#include <assert.h>

namespace new_muduo {
    const int Channel::kNoneEvent = 0;
    const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
    const int Channel::kWriteEvent = EPOLLOUT;

    Channel::Channel(EventLoop* loop, int fd)
        : loop_(loop),
        fd_(fd),
        events_(0),
        revents_(0),
        index_(-1),
        et_(true),  // 默认开启 ET 模式
        logHup_(true),
        tied_(false),
        eventHandling_(false),
        addedToLoop_(false) {
    }

    Channel::~Channel() {
        assert(!eventHandling_);
        assert(!addedToLoop_);
    }

    void Channel::tie(const std::shared_ptr<void>& obj) {
        tie_ = obj;
        tied_ = true;
    }

    void Channel::update() {
        addedToLoop_ = true;
        loop_->updateChannel(this);
    }

    void Channel::remove() {
        assert(isNoneEvent());
        addedToLoop_ = false;
        loop_->removeChannel(this);
    }

    void Channel::handleEvent(Timestamp receiveTime) {
        std::shared_ptr<void> guard;
        if (tied_) {
            guard = tie_.lock();
            if (guard) {
                handleEventWithGuard(receiveTime);
            }
        }
        else {
            handleEventWithGuard(receiveTime);
        }
    }

    void Channel::handleEventWithGuard(Timestamp receiveTime) {
        eventHandling_ = true;

        if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
            if (logHup_) {
                LOG_WARN << "fd = " << fd_ << " Channel::handleEvent() EPOLLHUP";
            }
            if (closeCallback_) closeCallback_();
        }

        if (revents_ & EPOLLERR) {
            if (errorCallback_) errorCallback_();
        }
        if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
            if (readCallback_) readCallback_(receiveTime);
        }
        if (revents_ & EPOLLOUT) {
            if (writeCallback_) writeCallback_();
        }
        eventHandling_ = false;
    }

    std::string Channel::reventsToString() const {
        return eventsToString(revents_);
    }

    std::string Channel::eventsToString() const {
        return eventsToString(events_);
    }

    std::string Channel::eventsToString(int ev) const {
        std::ostringstream oss;
        oss << fd_ << ": ";
        if (ev & EPOLLIN)  oss << "IN ";
        if (ev & EPOLLPRI) oss << "PRI ";
        if (ev & EPOLLOUT) oss << "OUT ";
        if (ev & EPOLLHUP) oss << "HUP ";
        if (ev & EPOLLRDHUP) oss << "RDHUP ";
        if (ev & EPOLLERR) oss << "ERR ";
        return oss.str();
    }
}
