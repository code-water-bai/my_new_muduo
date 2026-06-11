#include "CoroEventLoop.h"
#include "CoroScheduler.h"
#include "../net/EventLoop.h"
#include "../net/TimerQueue.h"
#include <assert.h>


namespace new_muduo::coro {
	thread_local CoroEventLoop* t_coroEventLoopInThisThread = nullptr;

	CoroEventLoop* CoroEventLoop::current() {
		return t_coroEventLoopInThisThread;
	}

    CoroEventLoop::CoroEventLoop(EventLoop* loop)
        : loop_(loop),
        scheduler_(std::make_unique<CoroScheduler>(loop)),
        timerQueue_(std::make_unique<neo::TimerQueue>(loop)) {
        assert(t_coroEventLoopInThisThread == nullptr);
        t_coroEventLoopInThisThread = this;

        // 将 CoroScheduler 挂载到 EventLoop，每次 poll 返回后自动调用 processPendingResumes
        loop_->setCoroScheduler(scheduler_.get());
    }

    CoroEventLoop::~CoroEventLoop() {
        t_coroEventLoopInThisThread = nullptr;
    }

    void CoroEventLoop::loop() {
        loop_->loop();
    }

    void CoroEventLoop::quit() {
        loop_->quit();
    }

    void CoroEventLoop::postTask(std::coroutine_handle<> h) {
        scheduleResume(h);
    }

    void CoroEventLoop::runInLoop(const std::function<void()>& cb) {
        loop_->runInLoop(cb);
    }

    void CoroEventLoop::queueInLoop(const std::function<void()>& cb) {
        loop_->queueInLoop(cb);
    }

    void CoroEventLoop::scheduleResume(std::coroutine_handle<> h) {
        scheduler_->scheduleResume(h);
    }

    void CoroEventLoop::processPendingResumes() {
        scheduler_->processPendingResumes();
    }

    bool CoroEventLoop::isInLoopThread() const {
        return loop_->isInLoopThread();
    }

}

