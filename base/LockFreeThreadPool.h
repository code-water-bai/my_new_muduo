#ifndef NEO_MUDUO_BASE_LOCKFREETHREADPOOL_H
#define NEO_MUDUO_BASE_LOCKFREETHREADPOOL_H

#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <semaphore>        // C++20 counting_semaphore
#include <atomic>

#include "noncopyable.h"
#include "Thread.h"
#include "MPMCQueue.h"

namespace new_muduo {

// 基于无锁队列的线程池
// 任务入队/出队完全无锁（MPMCQueue），阻塞用 C++20 semaphore
// Capacity 必须是 2 的幂
template <size_t Capacity = 1024>
class LockFreeThreadPool : noncopyable {
public:
    using Task = std::function<void()>;

    explicit LockFreeThreadPool(const std::string& name = "LockFreePool")
        : name_(name),
          running_(false),
          numThreads_(0),
          slotsSem_(Capacity),     // 初始：Capacity 个空槽位
          itemsSem_(0) {           // 初始：0 个可用任务
    }

    ~LockFreeThreadPool() {
        if (running_) stop();
    }

    void setThreadNum(int n) {
        assert(!running_ && n > 0);
        numThreads_ = n;
    }

    void start() {
        assert(!running_ && numThreads_ > 0);
        running_ = true;
        threads_.reserve(numThreads_);

        for (int i = 0; i < numThreads_; ++i) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s-%d", name_.c_str(), i);
            threads_.emplace_back(
                new Thread(std::bind(&LockFreeThreadPool::runInThread, this), buf));
            threads_[i]->start();
        }
    }

    void stop() {
        if (!running_) return;

        running_ = false;

        // 释放足够多的信号量唤醒所有阻塞的工作线程
        for (int i = 0; i < numThreads_; ++i) {
            itemsSem_.release();
        }

        for (auto& t : threads_) {
            t->join();
        }
    }

    // 提交任务（生产者端，多线程安全，无锁）
    bool run(Task task) {
        if (!running_) return false;

        if (threads_.empty()) {
            task();               // 无工作线程，直接执行
            return true;
        }

        // ① 等待空槽位（背压，被阻塞的不是锁而是 semaphore）
        slotsSem_.acquire();

        if (!running_) return false;

        // ② 无锁入队
        bool ok = queue_.enqueue(std::move(task));
        assert(ok);  // 有空槽位就一定能入队

        // ③ 通知消费者
        itemsSem_.release();
        return true;
    }

    const std::string& name() const { return name_; }
    size_t approxSize() const { return queue_.approximate_size(); }

private:
    void runInThread() {
        while (running_) {
            // ① 等待任务（阻塞在 semaphore，零 CPU 消耗）
            itemsSem_.acquire();
            if (!running_) break;

            // ② 无锁出队
            Task task;
            if (queue_.dequeue(task)) {
                // ③ 释放空槽位
                slotsSem_.release();

                // ④ 执行任务
                task();
            }
            // false wakeup: dequeue 失败（极端竞态下可能），继续循环
        }
    }

    std::string name_;
    std::atomic<bool> running_;
    int numThreads_;

    MPMCQueue<Task, Capacity> queue_;                  // 无锁任务队列
    std::counting_semaphore<Capacity> slotsSem_;        // 空槽位计数
    std::counting_semaphore<Capacity> itemsSem_;        // 可用任务计数

    std::vector<std::unique_ptr<Thread>> threads_;
};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_LOCKFREETHREADPOOL_H