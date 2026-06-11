#pragma once

#include "../base/noncopyable.h"

#include <coroutine>
#include <queue>
#include <memory>

namespace new_muduo {
    class EventLoop;
}  // namespace neo

namespace new_muduo::coro {


    class CoroScheduler : noncopyable {
    public:
        explicit CoroScheduler(EventLoop* loop);
        ~CoroScheduler();

        // 将协程加入恢复队列
        void scheduleResume(std::coroutine_handle<> h);

        // 批量恢复所有待恢复协程（由 EventLoop 在 poll 返回后调用）
        void processPendingResumes();

        EventLoop* getLoop() const { return loop_; }

    private:
        EventLoop* loop_;
        std::queue<std::coroutine_handle<>> pendingCoroutines_;
    };

}  // namespace neo::coro