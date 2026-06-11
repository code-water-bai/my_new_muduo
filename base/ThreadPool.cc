#include "ThreadPool.h"

#include <cassert>
#include <cstdio>
#include <algorithm>

namespace new_muduo {

ThreadPool::ThreadPool(const std::string& name)
    : name_(name),
      running_(false),
      numThreads_(0),
      batchSize_(kMinBatchSize),
       minThreads_(4),
        maxThreads_(128),
        idleTimeout_(100),
    queueHighWatermark_(500),
    queueLowWatermark_(20),
    idleThreadThreshold_(4)

{
}

ThreadPool::~ThreadPool() {
    if (running_) {
        stop();
    }
}

void ThreadPool::setThreadNum(int numThreads) {
    assert(!running_);
    assert(numThreads > 0);
    numThreads_ = numThreads;
}

void ThreadPool::start() {
    assert(!running_);
    assert(numThreads_ > 0);
    running_ = true;

    threads_.reserve(numThreads_);

    for (int i = 0; i < numThreads_; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s-%d", name_.c_str(), i);

        threads_.emplace_back(
            new Thread(std::bind(&ThreadPool::runInThread, this), buf)
        );
        threads_[i]->start();
    }
}

void ThreadPool::stop() {
    if (!running_) return;

    running_ = false;
    notEmpty_.notify_all();

    for (auto& thread : threads_) {
        thread->join();
    }
}

void ThreadPool::run(Task task) {
    if (threads_.empty()) {
        task();
    } else {
        std::unique_lock<std::mutex> lock(mutex_);

        notFull_.wait(lock, [this]() {
            return !running_ || queue_.size() < kMaxQueueSize;
        });

        if (!running_) return;

        queue_.push_back(std::move(task));
        notEmpty_.notify_one();
        uint64_t cnt = runCounter_.fetch_add(1, std::memory_order_relaxed);
        if (cnt % SAMPLE_INTERVAL == 0) {
            maybeAdjust();
        }
    }
}

// ========== 核心优化：批量取任务 ==========

size_t ThreadPool::fillLocalCache(ThreadLocalCache& cache) {

    if (cache.index < cache.tasks.size()) return cache.tasks.size() - cache.index;

    std::unique_lock<std::mutex> lock(mutex_);

    if (model_ == CACHE) {
        notEmpty_.wait_for(lock, idleTimeout_, [this]() {
            return !running_ || !queue_.empty();
            });

        if (queue_.empty()) {
            if (activeThreads_.load(std::memory_order_relaxed) > minThreads_) {
                return 0;
            }
            if (!running_) return 0;
            return -1;
        }
    }
    else {
        notEmpty_.wait(lock, [this]() {
            return !running_ || !queue_.empty();
            });

        if (queue_.empty() && !running_) return 0;
    }

    if (queue_.size() > batchSize_ * 2) {
        batchSize_ = std::min(batchSize_ * 2, kMaxBatchSize);
    }
    else if (queue_.size() < batchSize_ / 2) {
        batchSize_ = std::max(batchSize_ / 2, kMinBatchSize);
    }

    size_t n = std::min(batchSize_, queue_.size());

    cache.tasks.clear();
    cache.index = 0;
    cache.tasks.reserve(n);

    for (size_t i = 0; i < n; i++) {
        cache.tasks.emplace_back(std::move(queue_[i]));
    }
    queue_.erase(queue_.begin(), queue_.begin() + n);

    if (!queue_.empty()) {
        notEmpty_.notify_one();
    }

    if (queue_.size() < kMaxQueueSize * 3 / 4) {
        notFull_.notify_all();
    }

    return n;

}

void ThreadPool::maybeAdjust()
{
    if (model_ == FIXED) return;

    bool expected = false;
    if (!adjusting_.compare_exchange_strong(expected, true)) {
        return;
    }

    size_t qlen = queueLength_.load(std::memory_order_relaxed);
    size_t active = activeThreads_.load(std::memory_order_relaxed);
    auto now = std::chrono::steady_clock::now();

    if (qlen > queueHighWatermark_ && active < maxThreads_) {
        if (now - lastScaleUpTime_ > cooldownMs_) {
            size_t toAdd = std::min(maxThreads_ - active, size_t(2));
            for (size_t i = 1; i <= toAdd; ++i) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s-%d", name_.c_str(), 1+active);
                threads_.emplace_back(
                    new Thread(std::bind(&ThreadPool::runInThread, this), buf)
                );
                threads_[i]->start();
            }
            lastScaleUpTime_ = now;
        }
    }
    adjusting_.store(false);
}

void ThreadPool::runInThread() {
    ThreadLocalCache cache;
    activeThreads_.fetch_add(1, std::memory_order_relaxed);

    while (running_) {
        size_t available = fillLocalCache(cache);
        if (available == 0) break;
        else if (available == -1) continue;

        // 无锁消费本地缓存中的所有任务
        while (cache.index < cache.tasks.size()) {
            Task& task = cache.tasks[cache.index];
            cache.index++;
            task();
        }
    }
}

size_t ThreadPool::queueSize() const {
    return queueLength_.load(std::memory_order_acquire);
}

}  // namespace neo_muduo