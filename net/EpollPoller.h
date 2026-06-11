#pragma once

#include "Poller.h"

#include <sys/epoll.h>
#include <vector>

namespace new_muduo {

    // ============================================================
    // EPollPoller - 基于 epoll(7) 的 I/O 复用，支持 ET 模式
    // ============================================================
    class EPollPoller : public Poller {
    public:
        explicit EPollPoller(EventLoop* loop);
        ~EPollPoller() override;

        Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
        void updateChannel(Channel* channel) override;
        void removeChannel(Channel* channel) override;

    private:
        static const int kInitEventListSize = 16;

        static const char* operationToString(int op);

        void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
        void update(int operation, Channel* channel);

        using EventList = std::vector<struct epoll_event>;

        int epollfd_;
        EventList events_;
    };

}  // namespace neo