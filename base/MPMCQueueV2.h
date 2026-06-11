#ifndef NEO_MUDUO_BASE_MPMCQUEUEV2_H
#define NEO_MUDUO_BASE_MPMCQUEUEV2_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <type_traits>
#include <utility>
#include <functional>

// ============================================================
// 平台抽象：128 位 CAS
// ============================================================
#if defined(_MSC_VER)
    #include <intrin.h>
    #define NEO_MUDUO_DWCAS _InterlockedCompareExchange128
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(__x86_64__) || defined(_M_X64)
        #define NEO_MUDUO_DWCAS __sync_val_compare_and_swap
    #else
        #error "128-bit CAS requires x86-64"
    #endif
#else
    #error "Unsupported compiler for 128-bit CAS"
#endif

namespace neo_muduo {

// ============================================================
// 128 位原子结构：{ value(64bit), tag(64bit) }
// 对齐 16 字节（cmpxchg16b 要求）
// ============================================================
struct alignas(16) DoubleWord {
    uint64_t value;   // 业务值（位置）
    uint64_t tag;     // 版本号（单调递增防 ABA）

    bool operator==(const DoubleWord& rhs) const {
        return value == rhs.value && tag == rhs.tag;
    }
    bool operator!=(const DoubleWord& rhs) const {
        return !(*this == rhs);
    }
};

// 128 位原子 CAS：如果 *target == expected，写入 desired，返回 true
inline bool dwCAS(
    DoubleWord* target,
    const DoubleWord& expected,
    const DoubleWord& desired) noexcept
{
#if defined(_MSC_VER)
    // MSVC: _InterlockedCompareExchange128 返回 1 表示成功
    //       它通过指针参数返回旧值，但我们只需要成功/失败
    DoubleWord expectedCopy = expected;
    return _InterlockedCompareExchange128(
        reinterpret_cast<volatile int64_t*>(target),
        static_cast<int64_t>(desired.value),
        static_cast<int64_t>(desired.tag),
        reinterpret_cast<int64_t*>(&expectedCopy)) == 1;

#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang: __sync_val_compare_and_swap 比较 16 字节
    //            相等则交换并返回旧值，不等则返回旧值不变
    //            我们通过比较返回值与期望值判断是否成功
    __int128* ptr = reinterpret_cast<__int128*>(target);

    __int128 exp;
    std::memcpy(&exp, &expected, sizeof(expected));

    __int128 des;
    std::memcpy(&des, &desired, sizeof(desired));

    __int128 old = __sync_val_compare_and_swap(ptr, exp, des);

    DoubleWord oldDw;
    std::memcpy(&oldDw, &old, sizeof(old));

    return oldDw == expected;
#endif
}

// ============================================================
// ABA-proof MPMC 队列
// 每个 cell 的 sequence 用 128 位 {value, tag}
// 实际只需要 value 做判断，但 CAS 保证 tag 同步更新
// ============================================================
template <typename T, size_t Capacity>
class MPMCQueueV2 {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2");

    static constexpr size_t kMask = Capacity - 1;

    struct Cell {
        // sequence.value: 当前槽位的"epoch"
        // sequence.tag  : 防 ABA 版本号（与 enqueuePos.tag 同步）
        alignas(16) DoubleWord sequence;
        T data;
    };

public:
    MPMCQueueV2() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.value = i;
            buffer_[i].sequence.tag   = 0;
        }

        enqueuePos_.value = 0;
        enqueuePos_.tag   = 0;

        dequeuePos_.value = 0;
        dequeuePos_.tag   = 0;
    }

    ~MPMCQueueV2() = default;

    MPMCQueueV2(const MPMCQueueV2&) = delete;
    MPMCQueueV2& operator=(const MPMCQueueV2&) = delete;

    // -------------------------------------------------------
    // 入队
    // -------------------------------------------------------
    bool enqueue(T item) {
        Cell* cell = nullptr;
        DoubleWord pos = atomicLoad(enqueuePos_);

        for (;;) {
            cell = &buffer_[pos.value & kMask];
            DoubleWord seq = atomicLoad(cell->sequence);

            // 入队条件：seq.value == pos.value（槽位可写）
            if (seq.value == pos.value) {
                DoubleWord next;
                next.value = pos.value + 1;
                // tag 保持与 pos.tag 一致，防 ABA：
                // 如果 enqueuePos 绕了一圈回到相同 value，
                // tag 一定不同，CAS 必然失败
                next.tag = pos.tag;

                if (dwCAS(&enqueuePos_, pos, next)) {
                    // 成功占用该槽位
                    break;
                }
                // CAS 失败 → 重新读取 pos
                pos = atomicLoad(enqueuePos_);
            } else if (seq.value < pos.value) {
                // 队列满：pos 已经超过该 cell 的 seq + Capacity
                // （生产者太快，把整个环填满了）
                return false;
            } else {
                // seq.value > pos.value → pos 落后了，追赶
                pos = atomicLoad(enqueuePos_);
            }
        }

        // 写入数据
        cell->data = std::move(item);

        // 标记为已填充
        DoubleWord filled;
        filled.value = pos.value + 1;       // 奇数 = 已填充
        filled.tag   = pos.tag;             // 版本号不变
        atomicStore(cell->sequence, filled);

        return true;
    }

    // -------------------------------------------------------
    // 出队
    // -------------------------------------------------------
    bool dequeue(T& out) {
        Cell* cell = nullptr;
        DoubleWord pos = atomicLoad(dequeuePos_);

        for (;;) {
            cell = &buffer_[pos.value & kMask];
            DoubleWord seq = atomicLoad(cell->sequence);

            // 出队条件：seq.value == pos.value + 1（槽位已填充）
            if (seq.value == pos.value + 1) {
                DoubleWord next;
                next.value = pos.value + 1;
                next.tag   = pos.tag;

                if (dwCAS(&dequeuePos_, pos, next)) {
                    break;
                }
                pos = atomicLoad(dequeuePos_);
            } else if (seq.value < pos.value + 1) {
                // 队列空
                return false;
            } else {
                pos = atomicLoad(dequeuePos_);
            }
        }

        // 读取数据
        out = std::move(cell->data);

        // 标记为空闲：seq.value = pos.value + Capacity（下一轮的起始值）
        DoubleWord empty;
        empty.value = pos.value + Capacity;
        // tag 递增，确保即使 value 绕回，{value, tag} 也是全局唯一的
        empty.tag   = pos.tag + 1;
        atomicStore(cell->sequence, empty);

        return true;
    }

    size_t approximate_size() const {
        DoubleWord e = atomicLoad(enqueuePos_);
        DoubleWord d = atomicLoad(dequeuePos_);
        return (e.value >= d.value) ? (e.value - d.value) : 0;
    }

    bool empty() const {
        DoubleWord e = atomicLoad(enqueuePos_);
        DoubleWord d = atomicLoad(dequeuePos_);
        return d.value >= e.value;
    }

private:
    // 原子读取 DoubleWord（128 位 load，x86-64 上对齐读是原子的）
    static DoubleWord atomicLoad(const DoubleWord& src) {
        DoubleWord result;
#if defined(_MSC_VER)
        _ReadWriteBarrier();
        result = src;
        _ReadWriteBarrier();
#else
        __atomic_load(reinterpret_cast<const __int128*>(&src),
                       reinterpret_cast<__int128*>(&result),
                       __ATOMIC_ACQUIRE);
#endif
        return result;
    }

    // 原子写入 DoubleWord
    static void atomicStore(DoubleWord& dst, const DoubleWord& val) {
#if defined(_MSC_VER)
        _ReadWriteBarrier();
        dst = val;
        _ReadWriteBarrier();
#else
        __atomic_store(reinterpret_cast<__int128*>(&dst),
                        reinterpret_cast<const __int128*>(&val),
                        __ATOMIC_RELEASE);
#endif
    }

    Cell buffer_[Capacity];
    alignas(16) DoubleWord enqueuePos_;   // { value=游标, tag=版本 }
    alignas(16) DoubleWord dequeuePos_;   // { value=游标, tag=版本 }
};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_MPMCQUEUEV2_H