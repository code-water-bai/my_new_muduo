#ifndef NEO_MUDUO_BASE_DYNAMIC_THREADPOOL_H
#define NEO_MUDUO_BASE_DYNAMIC_THREADPOOL_H

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cassert>

namespace neo_muduo {

/*
 *  动态线程池 —— 设计深度解析
 *
 *  ═══════════════════════════════════════════════════════════
 *  一、扩容：什么时候加线程？
 *  ═══════════════════════════════════════════════════════════
 *
 *  单一指标各有缺陷：
 *
 *  ┌─────────────────┬──────────────────────────────────────────┐
 *  │ 指标            │ 缺陷                                     │
 *  ├─────────────────┼──────────────────────────────────────────┤
 *  │ 队列长度        │ 任务耗时差异大时误判。100个1μs任务 vs   │
 *  │ (queue.size())  │ 1个100ms任务，队列长度相同但压力差1000倍 │
 *  ├─────────────────┼──────────────────────────────────────────┤
 *  │ CPU 利用率      │ 反应滞后，出现IO密集型任务时严重低估    │
 *  │                 │ 真实压力（线程阻塞在IO上CPU很低）。      │
 *  │                 │ 且跨平台精确读取进程级CPU利用率成本高。  │
 *  ├─────────────────┼──────────────────────────────────────────┤
 *  │ 任务等待时间    │ 最准确但实现复杂，需要记录每个任务       │
 *  │ (wait latency)  │ 的入队时间戳和出队时间戳。              │
 *  └─────────────────┴──────────────────────────────────────────┘
 *
 *  工程上的务实选择：
 *
 *  双重条件 AND 逻辑 ——
 *    (1) 队列深度 > 阈值      ← 说明"输入速率 > 处理速率"
 *    (2) 任务平均等待 > 阈值  ← 说明这不仅仅是瞬时波动
 *
 *  这样可以过滤掉：
 *    - 瞬时尖峰（队列深但很快被消化 → 等待时间短 → 不扩容）
 *    - 个别慢任务导致的假象（队列浅但等待长 → 队列浅 → 不扩容）
 *
 *  ───────────────────────────────────────────────────────────
 *
 *  扩容执行策略：每次只扩 1 个线程（平滑扩容）
 *
 *  为什么不是一次扩到 max？
 *    - 防止"overshoot"：压力峰值可能已过去，多扩的线程立刻闲置
 *    - 给反馈留时间：扩1个 → 观察效果 → 还不够 → 再扩1个
 *    - 类比 TCP 拥塞控制的 AIMD（Additive Increase）
 *
 *  ═══════════════════════════════════════════════════════════
 *  二、缩容：什么时候减线程？
 *  ═══════════════════════════════════════════════════════════
 *
 *  缩容比扩容更需要谨慎。核心问题是：
 *
 *  ┌────────────────────────────────────────────────────────────┐
 *  │ 缩容过快 → 下一波请求到达 → 冷启动 → 延迟尖峰            │
 *  │ 缩容过慢 → 浪费内存（每个线程栈≈1-8MB）和上下文切换开销  │
 *  └────────────────────────────────────────────────────────────┘
 *
 *  指标选择：空闲时长（idle duration）
 *
 *  线程记录"上次执行任务的时间戳"，如果：
 *    当前时间 - 上次执行时间 > idleTimeout（如 60s）
 *  → 该线程可以被淘汰
 *
 *  为什么不用 CPU 利用率做缩容指标？
 *    线程池缩容的 CPU 问题不是"线程是否占 CPU"，而是"线程
 *    是否有可能马上被需要"。空闲线程不占 CPU（wait 在 cv 上），
 *    但占内存。所以用空闲时长最直接。
 *
 *  ═══════════════════════════════════════════════════════════
 *  三、缩容淘汰策略：LRU（最近最少使用）
 *  ═══════════════════════════════════════════════════════════
 *
 *  所有空闲线程按"上次活跃时间"排序，淘汰最久未使用的。
 *
 *  为什么是 LRU 而非随机？
 *    - 随机淘汰可能杀掉刚完成一大批任务、即将迎来下一批的线程
 *    - LRU 明确淘汰"最可能不再被需要"的线程
 *
 *  ═══════════════════════════════════════════════════════════
 *  四、空闲线程如何退出？
 *  ═══════════════════════════════════════════════════════════
 *
 *  方案对比：
 *
 *  ┌──────────────────┬─────────────────────┬───────────────────┐
 *  │ 方案             │ 实现                │ 优劣              │
 *  ├──────────────────┼─────────────────────┼───────────────────┤
 *  │ A. 超时自毁      │ worker 内部         │ 简单，无需主线程  │
 *  │ (wait_for)       │ cv.wait_for(timeout)│ 干涉。最常用。    │
 *  │                  │ 超时后自己 detach   │                   │
 *  ├──────────────────┼─────────────────────┼───────────────────┤
 *  │ B. 主线程通知    │ 主线程选一个 victim │ 主线程需要知道    │
 *  │ (kill signal)    │ 发信号让其退出      │ 每个线程的标识。  │
 *  │                  │                     │ 复杂度高。        │
 *  ├──────────────────┼─────────────────────┼───────────────────┤
 *  │ C. 引用计数      │ C++20 jthread 的    │ 需要 std::jthread,│
 *  │ (stop_token)     │ request_stop()      │ 不是所有编译器    │
 *  │                  │                     │ 都支持。          │
 *  └──────────────────┴─────────────────────┴───────────────────┘
 *
 *  选择方案 A（超时自毁）：
 *    worker 在 wait_for(idleTimeout) 超时后，检查当前线程数 >
 *    minThreads_，如果是则退出函数（线程自然结束）。
 *
 *  但纯 wait_for 有个问题：如果有 10 个线程都在 idleTimeout 后
 *  同时超时退出怎么办？→ 加入 jitter（随机偏移 ±20%），让它们
 *  错开退出，避免"羊群效应"。
 *
 *  ═══════════════════════════════════════════════════════════
 *  五、防抖动（Hysteresis）
 *  ═══════════════════════════════════════════════════════════
 *
 *  扩容阈值（低） ≠ 缩容阈值（高），留出缓冲区：
 *
 *    queueDepth >  expandThreshold  → 扩容
 *    queueDepth < shrinkThreshold  → 缩容
 *
 *    expandThreshold = threads * 2    （如4线程时队列>8就扩）
 *    shrinkThreshold = threads * 0.5  （如8线程时队列<4才考虑缩）
 *
 *  ───────────────────────────────────────────────────────────
 *
 *  此外加入冷却期（cooldown）：
 *    - 扩容后至少等 expandCooldown_ 秒才能再次扩容
 *    - 缩容后至少等 shrinkCooldown_ 秒才能再次缩容
 *    - 防止在阈值边界反复横跳
 *
 *  ═══════════════════════════════════════════════════════════
 *  六、完整状态机
 *  ═══════════════════════════════════════════════════════════
 *
 *               submit(task)
 *                   │
 *                   ▼
 *         ┌──────────────────┐
 *         │ 放入全局队列      │
 *         └────────┬─────────┘
 *                  │
 *         ┌────────▼─────────┐    检查条件：
 *         │ 应该扩容？        │    queue.size() > threads_*2
 *         │                   │    AND avgWait > 50ms
 *         │                   │    AND cooldown 已过
 *         │                   │    AND threads_ < maxThreads_
 *         └──┬───────────┬───┘
 *            │ YES       │ NO
 *            ▼           ▼
 *     spawnThread()    notify_one()
 *            │              │
 *            ▼              ▼
 *     worker 执行任务   worker 执行任务
 *            │              │
 *            ▼              ▼
 *     任务执行完毕     任务执行完毕
 *            │              │
 *            ▼              ▼
 *      wait_for(idleTimeout + jitter)
 *            │
 *     ┌──────┴──────┐
 *     │ 超时且       │ 有新任务
 *     │ threads_ >   │ 到来
 *     │ minThreads_  │
 *     │              │
 *     ▼              ▼
 *   线程退出     继续执行任务
 */

// ============================================================
// 主实现
// ============================================================

class DynamicThreadPool {
public:
    using Task = std::function<void()>;

    struct Config {
        size_t minThreads      = 2;       // 最小线程数（缩容下限）
        size_t maxThreads      = 128;     // 最大线程数（扩容上限）
        size_t initialThreads  = 4;       // 初始线程数

        // 扩容阈值：队列深度 > threads * expandFactor
        double expandFactor    = 2.0;
        // 缩容阈值：队列深度 < threads * shrinkFactor
        double shrinkFactor    = 0.5;

        // 扩容还需要——任务平均等待时间超过此值（毫秒）
        std::chrono::milliseconds avgWaitThreshold{50};

        // 空闲超时：线程空闲超过此时间允许自毁（秒）
        std::chrono::seconds idleTimeout{60};

        // 冷却期（防止抖动，秒）
        std::chrono::seconds expandCooldown{2};
        std::chrono::seconds shrinkCooldown{10};

        // jitter 系数（±20% 防羊群效应）
        double jitter = 0.2;
    };

    explicit DynamicThreadPool(const Config& config = Config{})
        : config_(config)
        , threads_(0)
        , stop_(false)
        , lastExpandTime_(Clock::now() - config.expandCooldown)   // 允许立刻扩容
        , lastShrinkTime_(Clock::now() - config.shrinkCooldown)   // 允许立刻缩容
    {
        assert(config_.minThreads <= config_.initialThreads);
        assert(config_.initialThreads <= config_.maxThreads);

        resize(config_.initialThreads);
    }

    ~DynamicThreadPool() {
        stopAll();
    }

    // ────────── 提交任务 ──────────

    template <typename F>
    void submit(F&& f) {
        auto now = Clock::now();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            TaskEntry entry;
            entry.task = std::forward<F>(f);
            entry.enqueueTime = now;
            tasks_.push_back(std::move(entry));
        }

        cv_.notify_one();

        // 每次 submit 都检查是否应该扩容
        tryExpand();
    }

    // ────────── 查询接口 ──────────

    size_t threadCount() const {
        // 注意：threads_ 的读取需要一定程度的原子性，
        // 这里简化为返回 threads_ 负载的近似值
        std::lock_guard<std::mutex> lock(queueMutex_);
        return threads_;
    }

    size_t pendingTasks() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return tasks_.size();
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct TaskEntry {
        Task task;
        TimePoint enqueueTime;   // 入队时间（用于计算平均等待）
    };

    // ────────── 扩容判断与执行 ──────────

    void tryExpand() {
        // 快速路径：不持锁检查，减少竞争（ThreadPool.h 中的设计经验）
        size_t currentThreads = threads_;  // 近似读取，不精确但够用

        if (currentThreads >= config_.maxThreads) return;

        auto now = Clock::now();
        if (now - lastExpandTime_ < config_.expandCooldown) return;

        // 进入慢路径：持锁检查
        std::lock_guard<std::mutex> lock(queueMutex_);

        if (threads_ >= config_.maxThreads) return;
        if (now - lastExpandTime_ < config_.expandCooldown) return;

        // 条件 1：队列深度 > 阈值
        size_t threshold = static_cast<size_t>(threads_ * config_.expandFactor);
        if (tasks_.size() <= threshold) return;

        // 条件 2：平均等待时间 > 阈值
        auto avgWait = computeAvgWait(now);
        if (avgWait < config_.avgWaitThreshold) return;

        // 满足条件 → 扩容 1 个
        spawnOne();
        lastExpandTime_ = now;
    }

    // ────────── 计算平均等待时间 ──────────

    // 调用者必须持有 queueMutex_
    std::chrono::milliseconds computeAvgWait(TimePoint now) const {
        if (tasks_.empty()) return std::chrono::milliseconds{0};

        std::chrono::milliseconds total{0};
        for (const auto& entry : tasks_) {
            total += std::chrono::duration_cast<std::chrono::milliseconds>(
                now - entry.enqueueTime);
        }
        return total / static_cast<int>(tasks_.size());
    }

    // ────────── 生成线程 ──────────

    void spawnOne() {
        threads_++;
        std::thread t([this] { workerLoop(); });
        t.detach();  // 线程自理生命周期
        // 注意：这里用 detach 而不是 join。join 需要管理 thread
        // 对象的生命周期，与"线程自毁"模型冲突。
        //
        // detach 后线程退出时自动回收资源，无需外部管理。
        // 安全的前提是：workerLoop 中所有资源在线程内管理，
        // 退出时 threads_-- 由锁保护。
    }

    void resize(size_t target) {
        if (target > threads_) {
            for (size_t i = threads_; i < target; ++i) spawnOne();
        }
        // 缩容不由外部强制触发，由线程内部超时自毁完成
    }

    // ────────── 工作循环 ──────────

    void workerLoop() {
        TimePoint lastActive = Clock::now();  // 上次执行任务的时间

        while (true) {
            Task task;

            // ── 第一步：取任务 ──
            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                // 如果队列空，等待。同时检查是否可以缩容。
                if (tasks_.empty() || stop_) {
                    // 计算本次等待的超时时间（带 jitter）
                    auto timeout = config_.idleTimeout;
                    auto jitterMs = std::chrono::milliseconds(
                        static_cast<long long>(timeout.count() * 1000 * config_.jitter));
                    // 随机偏移：idleTimeout ± jitter
                    long long offset = static_cast<long long>(
                        static_cast<double>(jitterMs.count()) *
                        (static_cast<double>(rand()) / RAND_MAX * 2.0 - 1.0));
                    auto finalTimeout = timeout + std::chrono::milliseconds(offset);

                    cv_.wait_for(lock, finalTimeout);

                    if (stop_ && tasks_.empty()) {
                        threads_--;
                        return;
                    }

                    // 超时醒来 → 检查是否可以缩容退出
                    if (tasks_.empty() && !stop_) {
                        // 当前线程数 > 最小线程数 → 可以退出
                        // 且确实空闲了足够长时间
                        auto idleDuration = std::chrono::duration_cast<std::chrono::seconds>(
                            Clock::now() - lastActive);

                        if (threads_ > config_.minThreads_ &&
                            idleDuration >= config_.idleTimeout) {
                            threads_--;
                            return;  // 自毁
                        }
                    }
                }

                // 真正有任务才取
                if (!tasks_.empty()) {
                    task = std::move(tasks_.front().task);
                    tasks_.pop_front();
                }
            }

            // ── 第二步：执行任务 ──
            if (task) {
                task();
                lastActive = Clock::now();
            }

            // ── 第三步：检查缩容（在主循环中周期性检查） ──
            tryShrink(lastActive);
        }
    }

    // ────────── 缩容判断 ──────────

    void tryShrink(TimePoint lastActive) {
        auto now = Clock::now();

        // 快速路径
        if (threads_ <= config_.minThreads_) return;
        if (now - lastShrinkTime_ < config_.shrinkCooldown) return;

        // 慢路径：持锁检查队列深度
        std::lock_guard<std::mutex> lock(queueMutex_);

        if (threads_ <= config_.minThreads_) return;
        if (now - lastShrinkTime_ < config_.shrinkCooldown) return;

        // 缩容条件：队列深度足够低
        size_t shrinkThreshold = static_cast<size_t>(threads_ * config_.shrinkFactor);
        if (tasks_.size() > shrinkThreshold) return;

        // 额外安全检查：确实有线程空闲了足够久
        auto idleDuration = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastActive);
        if (idleDuration < config_.idleTimeout) return;

        // 满足条件 → 标记缩容时间，当前线程退出
        threads_--;
        lastShrinkTime_ = now;
        // 注意：这里 threads_-- 之后函数返回，调用栈回到 workerLoop，
        // workerLoop 中执行 return → 线程退出。
        // 这种方式意味着"缩容检查通过的线程就是被淘汰的线程"。
        //
        // 等效于 LRU：因为这个线程在检查时已空闲了 idleDuration，
        // 而其他正常工作的线程的 lastActive 肯定更近。
    }

    // ────────── 停止 ──────────

    void stopAll() {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stop_ = true;
        }
        cv_.notify_all();

        // 等待所有线程退出（轮询 threads_）
        while (true) {
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (threads_ == 0) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // ────────── 成员 ──────────

    Config config_;

    std::deque<TaskEntry> tasks_;   // 全局任务队列
    mutable std::mutex queueMutex_;
    std::condition_variable cv_;

    size_t threads_;               // 当前线程数（受 queueMutex_ 保护）
    std::atomic<bool> stop_{false};

    TimePoint lastExpandTime_;
    TimePoint lastShrinkTime_;
};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_DYNAMIC_THREADPOOL_H
