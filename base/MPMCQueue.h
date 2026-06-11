#ifndef NEO_MUDUO_BASE_MPMCQUEUE_H
#define NEO_MUDUO_BASE_MPMCQUEUE_H

#include <atomic>
#include <cstddef>
#include <cassert>
#include <type_traits>
#include <utility>

namespace new_muduo {

// 有界无锁 MPMC 队列（Dmitry Vyukov 算法）
// 多生产者多消费者安全，无需互斥锁
// Capacity 必须是 2 的幂（位运算取模）
template <typename T, size_t Capacity>
class MPMCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2");
    static_assert(std::is_nothrow_move_constructible_v<T> ||
                  std::is_copy_constructible_v<T>,
                  "T must be movable or copyable");

    static constexpr size_t kMask = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

public:
    MPMCQueue() {
        // 初始化：每个 cell 的 sequence = 其索引
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueuePos_.store(0, std::memory_order_relaxed);
        dequeuePos_.store(0, std::memory_order_relaxed);
    }

    ~MPMCQueue() = default;

    // 禁止拷贝
    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    // 入队（生产者调用，多线程安全）
    // 返回 true 表示成功，false 表示队列满

    bool enqueue(T item) {
        Cell* cell = nullptr;
        size_t pos = enqueuePos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & kMask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // ① cell 可用（sequence == pos）
                if (enqueuePos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
                // CAS 失败 → pos 已更新，重试
            } else if (diff < 0) {
                // 队列满（新一轮的 enqueuePos 已经超过该 cell 的 sequence + Capacity）
                return false;
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }

        // ③ 写入数据
        cell->data = std::move(item);

        // ④ 标记为已填充（sequence = pos + 1）
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }


    bool dequeue(T& out) {
        Cell* cell = nullptr;
        size_t pos = dequeuePos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & kMask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                // ① cell 已填充（sequence == pos+1）
                if (dequeuePos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    // ② 成功占用该 slot
                    break;
                }
                // CAS 失败 → pos 已更新，重试
            } else if (diff < 0) {
                // 队列空（该 cell 还没有被填充）
                return false;
            } else {
                // 其他消费者正在读或已读完，pos 落后了 → 更新 pos 重试
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }

        // ③ 读取数据
        out = std::move(cell->data);
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    // 估算长度（非原子快照，仅用于监控）
    size_t approximate_size() const {
        size_t e = enqueuePos_.load(std::memory_order_relaxed);
        size_t d = dequeuePos_.load(std::memory_order_relaxed);
        return (e >= d) ? (e - d) : 0;
    }

    // 是否为空
    bool empty() const {
        return dequeuePos_.load(std::memory_order_relaxed) >=
               enqueuePos_.load(std::memory_order_relaxed);
    }

private:
    Cell buffer_[Capacity];
    std::atomic<size_t> enqueuePos_;   // 生产者游标（单调递增，不回绕）
    std::atomic<size_t> dequeuePos_;   // 消费者游标（单调递增，不回绕）
};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_MPMCQUEUE_H