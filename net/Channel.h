#pragma once

#include "../base/noncopyable.h"
#include "../base/Timestamp.h"

#include <functional>
#include <memory>
#include <string>

namespace new_muduo {

    class EventLoop;


    class Channel : noncopyable {
    public:
        using EventCallback = std::function<void()>;
        using ReadEventCallback = std::function<void(Timestamp)>;

        Channel(EventLoop* loop, int fd);
        ~Channel();

        void handleEvent(Timestamp receiveTime);

        void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
        void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
        void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
        void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

        int fd() const { return fd_; }
        int events() const { return events_; }
        void setRevents(int revt) { revents_ = revt; }
        bool isNoneEvent() const { return events_ == kNoneEvent; }

        // ET 模式开关
        void enableET() { et_ = true; }
        void disableET() { et_ = false; }
        bool isET() const { return et_; }

        void enableReading() { events_ |= kReadEvent; update(); }
        void disableReading() { events_ &= ~kReadEvent; update(); }
        void enableWriting() { events_ |= kWriteEvent; update(); }
        void disableWriting() { events_ &= ~kWriteEvent; update(); }
        void disableAll() { events_ = kNoneEvent; update(); }

        bool isReading() const { return events_ & kReadEvent; }
        bool isWriting() const { return events_ & kWriteEvent; }

        int index() const { return index_; }
        void setIndex(int idx) { index_ = idx; }

        EventLoop* ownerLoop() const { return loop_; }

        void remove();

        // for debug
        std::string reventsToString() const;
        std::string eventsToString() const;

        void tie(const std::shared_ptr<void>& obj);

        static const int kNoneEvent;
        static const int kReadEvent;
        static const int kWriteEvent;

    private:
        std::string eventsToString(int ev) const;
        void update();
        void handleEventWithGuard(Timestamp receiveTime);

        EventLoop* loop_;
        const int fd_;
        int events_;
        int revents_;
        int index_;  // used by EPollPoller
        bool et_;    // edge-triggered mode

        bool logHup_;
        bool tied_;
        bool eventHandling_;
        bool addedToLoop_;
        std::weak_ptr<void> tie_;
        std::shared_ptr<void> guard_;

        ReadEventCallback readCallback_;
        EventCallback writeCallback_;
        EventCallback closeCallback_;
        EventCallback errorCallback_;
    };

}  // namespace neo