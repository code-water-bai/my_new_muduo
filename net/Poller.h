#pragma once

#include "../base/noncopyable.h"
#include "../base/Timestamp.h"

#include <vector>
#include <map>

namespace new_muduo {

    class Channel;
    class EventLoop;

   
    class Poller : noncopyable {
    public:
        using ChannelList = std::vector<Channel*>;

        explicit Poller(EventLoop* loop);
        virtual ~Poller();

        virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;
        virtual void updateChannel(Channel* channel) = 0;
        virtual void removeChannel(Channel* channel) = 0;

        virtual bool hasChannel(Channel* channel) const;

        static Poller* newDefaultPoller(EventLoop* loop);

        void assertInLoopThread() const;

    protected:
        using ChannelMap = std::map<int, Channel*>;
        ChannelMap channels_;

    private:
        EventLoop* ownerLoop_;
    };

}  // namespace neo