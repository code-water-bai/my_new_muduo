#include "EpollPoller.h"
#include "Channel.h"
#include "../base/Logging.h"

#include <cassert>
#include <cerrno>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>

namespace new_muduo {
    EPollPoller::EPollPoller(EventLoop* loop)
        : Poller(loop),
        epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
        events_(kInitEventListSize) {
        if (epollfd_ < 0) {
            LOG_SYSFATAL << "EPollPoller::EPollPoller epoll_create1";
        }
    }


    EPollPoller::~EPollPoller() {
        ::close(epollfd_);
    }

    Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels) {
        int numEvents = ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
        int savedErrno = errno;

        Timestamp now(Timestamp::now());
        if (numEvents > 0) {
            LOG_TRACE << numEvents << " events happened";
            fillActiveChannels(numEvents, activeChannels);
            if (static_cast<size_t>(numEvents) == events_.size()) {
                events_.resize(events_.size() * 2);
            }
        }
        else if (numEvents == 0) {
            LOG_TRACE << "nothing happened";
        }
        else {
            if (savedErrno != EINTR) {
                errno = savedErrno;
                LOG_SYSERR << "EPollPoller::poll()";
            }
        }

        return now;
    }

    void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const {
        assert(static_cast<size_t>(numEvents) <= events_.size());
        for (int i = 0; i < numEvents; ++i) {
            Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
#ifndef NDEBUG
            int fd = channel->fd();
            auto it = channels_.find(fd);
            assert(it != channels_.end());
            assert(it->second == channel);
#endif
            channel->setRevents(events_[i].events);
            activeChannels->push_back(channel);
        }
    }

    void EPollPoller::updateChannel(Channel* channel) {
        assertInLoopThread();
        const int index = channel->index();
        LOG_TRACE << "fd = " << channel->fd()
            << " events = " << channel->events() << " index = " << index;
        if (index < 0) {
            assert(channels_.find(channel->fd()) == channels_.end());
            channels_[channel->fd()] = channel;
            update(EPOLL_CTL_ADD, channel);
        }
        else {
            assert(channels_.find(channel->fd()) != channels_.end());
            assert(channels_[channel->fd()] == channel);
            if (channel->isNoneEvent()) {
                update(EPOLL_CTL_DEL, channel);
                channel->setIndex(-1);
            }
            else {
                update(EPOLL_CTL_MOD, channel);
            }
        }
    }

    void EPollPoller::removeChannel(Channel* channel) {
        assertInLoopThread();
        LOG_TRACE << "fd = " << channel->fd();
        int fd = channel->fd();
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);
        assert(channel->isNoneEvent());
        int index = channel->index();
        assert(index == 1 || index == 2);
        size_t n = channels_.erase(fd);
        assert(n == 1);
        (void)n;

        if (index == 2) {
            update(EPOLL_CTL_DEL, channel);
        }
        channel->setIndex(-1);
    }

    void EPollPoller::update(int operation, Channel* channel) {
        struct epoll_event event;
        memset(&event, 0, sizeof(event));
        event.events = channel->events();
        event.data.ptr = channel;

        // ET 模式：如果 Channel 开启了 ET，在 epoll 层面也启用
        if (channel->isET()) {
            event.events |= EPOLLET;
        }

        int fd = channel->fd();
        LOG_TRACE << "epoll_ctl op = " << operationToString(operation)
            << " fd = " << fd << " event = { " << channel->eventsToString() << " }";
        if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) {
            if (operation == EPOLL_CTL_DEL) {
                LOG_SYSERR << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
            }
            else {
                LOG_SYSFATAL << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
            }
        }

        if (operation == EPOLL_CTL_ADD) {
            channel->setIndex(1);
        }
        else if (operation == EPOLL_CTL_DEL) {
            channel->setIndex(-1);
        }
        else {
            // EPOLL_CTL_MOD，index 保持为 2
            channel->setIndex(2);
        }
    }

    const char* EPollPoller::operationToString(int op) {
        switch (op) {
        case EPOLL_CTL_ADD: return "ADD";
        case EPOLL_CTL_DEL: return "DEL";
        case EPOLL_CTL_MOD: return "MOD";
        default: return "Unknown";
        }
    }

}  // namespace new_muduo

new_muduo::Poller* new_muduo::Poller::newDefaultPoller(new_muduo::EventLoop* loop) {
    return new new_muduo::EPollPoller(loop);
}