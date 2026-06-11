#include "CoroScheduler.h"
#include "../net/EventLoop.h"
#include "../base/Logging.h"

namespace new_muduo::coro {

    CoroScheduler::CoroScheduler(EventLoop* loop)
        : loop_(loop) {
    }

    CoroScheduler::~CoroScheduler() {
    }

    void CoroScheduler::scheduleResume(std::coroutine_handle<> h) {
        pendingCoroutines_.push(h);
    }

    void CoroScheduler::processPendingResumes() {
        while (!pendingCoroutines_.empty()) {
            auto h = pendingCoroutines_.front();
            pendingCoroutines_.pop();
            if (h && !h.done()) {
                h.resume();
            }
        }
    }

}  // namespace neo::coro