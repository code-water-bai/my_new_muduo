#pragma once

#include "UringEventLoopThread.h"
namespace new_muduo::io_uring {
    class UringEventLoopThreadPool : new_muduo::noncopyable {
    public:
        using ThreadInitCallback = std::function<void(EventLoop*)>;

        UringEventLoopThreadPool(EventLoop* baseLoop, const std::string& name)
            : baseLoop_(baseLoop),
            name_(name),
            started_(false),
            numThreads_(0),
            next_(0) {
        }

        ~UringEventLoopThreadPool() = default;

        void setThreadNum(int numThreads) { numThreads_ = numThreads; }

        void start(const ThreadInitCallback& cb) {
            assert(!started_);
            baseLoop_->assertInLoopThread();
            started_ = true;

            for (int i = 0; i < numThreads_; ++i) {
                char buf[name_.size() + 32];
                snprintf(buf, sizeof(buf), "%s%d", name_.c_str(), i);
                auto* t = new EventLoopThread(cb);
                threads_.push_back(std::unique_ptr<EventLoopThread>(t));
                loops_.push_back(t->startLoop());
            }

            if (numThreads_ == 0 && cb) {
                cb(baseLoop_);
            }
        }

        EventLoop* getNextLoop() {
            baseLoop_->assertInLoopThread();
            EventLoop* loop = baseLoop_;

            if (!loops_.empty()) {
                loop = loops_[next_];
                ++next_;
                if (static_cast<size_t>(next_) >= loops_.size()) {
                    next_ = 0;
                }
            }
            return loop;
        }

        std::vector<EventLoop*> getAllLoops() {
            if (loops_.empty()) {
                return { baseLoop_ };
            }
            return loops_;
        }

    private:
        EventLoop* baseLoop_;
        std::string name_;
        bool started_;
        int numThreads_;
        int next_;
        std::vector<std::unique_ptr<EventLoopThread>> threads_;
        std::vector<EventLoop*> loops_;
    };
}