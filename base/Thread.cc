#include "Thread.h"

#include <thread>
#include <chrono>
#include <cassert>


namespace new_muduo {

    namespace CurrentThread {
        thread_local int t_cachedTid = 0;
        thread_local char t_tidString[32] = { 0 };
        thread_local int t_tidStringLength = 6;
        thread_local const char* t_threadName = "unknown";
    }  // namespace CurrentThread

    namespace detail {

        pid_t gettid() {
            return static_cast<pid_t>(::syscall(SYS_gettid));
        }
    }

    void CurrentThread::cacheTid() {
        if (t_cachedTid == 0) {
            t_cachedTid = detail::gettid();
            t_tidStringLength = snprintf(t_tidString, sizeof(t_tidString), "%5d ", t_cachedTid);
        }
    }

    bool CurrentThread::isMainThread() {
        return tid() == ::getpid();
    }

// 线程局部存储：当前线程对应的 Thread 对象指针
static thread_local Thread* t_thread = nullptr;

Thread::Thread(ThreadFunc func, const std::string& name)
    : func_(std::move(func)),
      name_(name),
      started_(false),
      joined_(false) {
}

Thread::~Thread() {
    // 如果线程已启动且未 join，尝试 detach
    if (started_ && !joined_) {
        if (thread_ && thread_->joinable()) {
            thread_->detach();
        }
    }
}

void Thread::start() {
    assert(!started_);
    started_ = true;

    thread_ = std::make_unique<std::thread>([this]() {this->runInThread(); });
}

void Thread::join() {
    assert(started_);
    assert(!joined_);
    joined_ = true;

    if (thread_->joinable()) {
        thread_->join();
    }
}

void Thread::runInThread() {
    t_thread = this;

   
    try {
        func_();  // 执行用户函数（可能是 EventLoop::loop()）
    } catch (const std::exception& ex) {
        // 捕获异常，避免线程静默终止
        fprintf(stderr, "Thread %s caught exception: %s\n",
                name_.c_str(), ex.what());
        abort();
    } catch (...) {
        fprintf(stderr, "Thread %s caught unknown exception\n", name_.c_str());
        abort();
    }

    t_thread = nullptr;
}

Thread* Thread::currentThread() {
    return t_thread;
}

// 工具函数：睡眠指定微秒
void Thread::sleepUsec(int64_t usec) {
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

}  