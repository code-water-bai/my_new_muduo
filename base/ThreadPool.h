#ifndef NEO_MUDUO_BASE_THREADPOOL_H
#define NEO_MUDUO_BASE_THREADPOOL_H

#include <functional>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "noncopyable.h"
#include "Thread.h"

namespace new_muduo {

// 通用线程池（批量取任务优化版）
class ThreadPool : noncopyable {
public:
    using Task = std::function<void()>;
    enum model { FIXED, CACHE };

    explicit ThreadPool(const std::string& name = "ThreadPool");
    ~ThreadPool();

    void setThreadNum(int numThreads);
    void start();
    void stop();
    void setModel(model mod) {
        if (running_) return;
        model_ = mod;
    }//运行时不可更改
    void run(Task task);

    int threadNum() const { return numThreads_; }
    size_t queueSize() const;

    const std::string& name() const { return name_; }

  

private:
    // 线程本地任务缓存（无锁）
    struct ThreadLocalCache {
        std::vector<Task> tasks;
        size_t index = 0;
    };
    // 自适应批量大小
    static const size_t kMinBatchSize = 4;
    static const size_t kMaxBatchSize = 64;
    size_t batchSize_;


    std::string name_;
    std::atomic<bool> running_;
    int numThreads_;

    //动态调整策略
     model model_;
        //动态伸缩参数
    const size_t minThreads_;
    const size_t maxThreads_;
    const std::chrono::milliseconds idleTimeout_;
        //监控指标
    std::atomic<size_t> queueLength_{ 0 };              
    std::atomic<size_t> activeThreads_{ 0 };
        //调整控制
    std::atomic<bool> adjusting_{ false };
    std::chrono::steady_clock::time_point lastScaleUpTime_;
    const std::chrono::milliseconds cooldownMs_{ 1000 };
        // --- 阈值 ---
    const size_t queueHighWatermark_;                
    const size_t queueLowWatermark_;                 
    const size_t idleThreadThreshold_;
        //频率控制
    std::atomic<uint64_t> runCounter_{ 0 };
    static constexpr uint64_t SAMPLE_INTERVAL = 64;


    std::vector<std::unique_ptr<Thread>> threads_;
    std::vector<Task> queue_;                        // 全局任务队列

    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;

    static const size_t kMaxQueueSize = 10000;
private:
    void runInThread();                              // 线程工作函数
    size_t fillLocalCache(ThreadLocalCache& cache);  // 从全局队列批量取任务
    void maybeAdjust();
};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_THREADPOOL_H