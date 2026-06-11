#pragma once

#include "../base/noncopyable.h"
#include "../base/Timestamp.h"
#include "../base/Thread.h"
#include"Timerld.h"
#include <functional>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>

namespace new_muduo {

    class Channel;
    class Poller;
    class TimerQueue;
    namespace coro { class CoroScheduler; }
    using namespace coro;

    class EventLoop : noncopyable {
    public:
        using Functor = std::function<void()>;

        EventLoop();
        ~EventLoop();

        void loop();
        void quit();

        Timestamp pollReturnTime() const { return pollReturnTime_; }

        int64_t iteration() const { return iteration_; }

        // 线程安全：可在其他线程调用
        void runInLoop(Functor cb);
        void queueInLoop(Functor cb);

        size_t queueSize() const;

        // 定时器
        void runAt(Timestamp time, Functor cb);
        void runAfter(double delay, Functor cb);
        void runEvery(double interval, Functor cb);
        void cancel(TimerId timerId);

        // 唤醒
        void wakeup();

        // Channel 管理
        void updateChannel(Channel* channel);
        void removeChannel(Channel* channel);
        bool hasChannel(Channel* channel);

        // 线程检查
        void assertInLoopThread() const {
            if (!isInLoopThread()) {
                abortNotInLoopThread();
            }
        }
        bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

        // 协程集成
        void setCoroScheduler(CoroScheduler* scheduler) { coroScheduler_ = scheduler; }
        CoroScheduler* coroScheduler() const { return coroScheduler_; }

        // 每次 poll 返回后的回调钩子（CoroEventLoop 使用）
        void setPendingResumesCallback(std::function<void()> cb) { pendingResumesCallback_ = std::move(cb); }

        static EventLoop* getEventLoopOfCurrentThread();

    private:
        void abortNotInLoopThread() const;
        void handleRead();  // 唤醒
        void doPendingFunctors();

        using ChannelList = std::vector<Channel*>;

        bool looping_;
        std::atomic<bool> quit_;
        bool eventHandling_;
        bool callingPendingFunctors_;
        int64_t iteration_;
        const pid_t threadId_;
        Timestamp pollReturnTime_;

        std::unique_ptr<Poller> poller_;
        std::unique_ptr<TimerQueue> timerQueue_;

        int wakeupFd_;
        std::unique_ptr<Channel> wakeupChannel_;

        ChannelList activeChannels_;
        Channel* currentActiveChannel_;

        mutable std::mutex mutex_;
        std::vector<Functor> pendingFunctors_;

        CoroScheduler* coroScheduler_;
        std::function<void()> pendingResumesCallback_;
    };

}  // namespace neo