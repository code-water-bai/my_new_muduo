#pragma once

#include "../base/noncopyable.h"

#include <coroutine>
#include <memory>
#include"../net/Callbacks.h"
#define neo new_muduo

namespace new_muduo {
    class EventLoop;
    class TimerQueue;
}  // namespace neo

namespace new_muduo::coro{

    class CoroScheduler;

    
    class CoroEventLoop : noncopyable {
    public:
        explicit CoroEventLoop(EventLoop* loop);
        ~CoroEventLoop();

        // 每个线程只有一个 CoroEventLoop
        static CoroEventLoop* current();

        EventLoop* getRawLoop() const { return loop_; }

        // 启动事件循环
        void loop();

        void quit();

        // 提交协程任务
        void postTask(std::coroutine_handle<> h);

        // 提交普通函数任务
        void runInLoop(const std::function<void()>& cb);
        void queueInLoop(const std::function<void()>& cb);

        // 调度器接口
        void scheduleResume(std::coroutine_handle<> h);

        bool isInLoopThread() const;

        // 获取内部的定时器队列
        neo::TimerQueue* timerQueue() const { return timerQueue_.get(); }

    private:
        void processPendingResumes();

        EventLoop* loop_;
        std::unique_ptr<CoroScheduler> scheduler_;
        std::unique_ptr<neo::TimerQueue> timerQueue_;
    };

}  // namespace neo::coro