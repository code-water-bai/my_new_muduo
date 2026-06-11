#ifndef NEO_MUDUO_BASE_LFMEMORYPOOL_H
#define NEO_MUDUO_BASE_LFMEMORYPOOL_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <array>

#include "TaggedPointer.h"
#include "HazardPointer.h"

namespace new_muduo {

// ============================================================
// 无锁内存池（Slab 分配器）
//
// 设计：
//   - 按 2 的幂次分档：64B / 128B / 256B / 512B / 1K / 2K / 4K
//   - 每档一个 FreeList（Lock-Free Stack + TaggedPointer）
//   - 每线程一个 ThreadCache（批量从 FreeList 取/还，减少跨核竞争）
//   - Hazard Pointer 保护 pop 操作中的节点
// ============================================================

class LFMemoryPool {
public:
    // 块大小分档（2 的幂次，从 64 到 4096）
    static constexpr int kMinClass = 6;   // 2^6 = 64
    static constexpr int kMaxClass = 12;  // 2^12 = 4096
    static constexpr int kNumClasses = kMaxClass - kMinClass + 1;  // 7 档

    // 每档的 ThreadCache 容量
    static constexpr int kCacheCapacity = 16;

    // -------------------------------------------------------
    // 内部节点结构（FreeList 的节点）
    // -------------------------------------------------------
    struct FreeNode {
        FreeNode* next;
        // 实际数据从这里开始，但内存池只分配/回收整块
    };

    // -------------------------------------------------------
    // 无锁 FreeList：TaggedPointer 保护的栈
    // -------------------------------------------------------
    struct FreeList {
        // 使用 TaggedPointer 防 ABA
        // packed = {ptr(48bit), tag(16bit)}
        std::atomic<uint64_t> headPacked{0};

        // push：无锁入栈
        void push(FreeNode* node) {
            uint64_t oldPacked = headPacked.load(std::memory_order_acquire);
            uint64_t newPacked;

            do {
                FreeNode* oldHead = reinterpret_cast<FreeNode*>(
                    oldPacked & 0x0000FFFFFFFFFFFFULL);
                uint16_t oldTag = static_cast<uint16_t>(oldPacked >> 48);

                node->next = oldHead;

                // tag 不变，只更新指针
                uint64_t ptrPart = reinterpret_cast<uint64_t>(node);
                newPacked = ptrPart | (static_cast<uint64_t>(oldTag) << 48);

            } while (!headPacked.compare_exchange_weak(
                oldPacked, newPacked,
                std::memory_order_release,
                std::memory_order_acquire));
        }

        // pop：无锁出栈，用 Hazard Pointer 保护
        FreeNode* pop() {
            using HP = HazardPointerDomain<FreeNode>;

            uint64_t oldPacked = headPacked.load(std::memory_order_acquire);

            for (;;) {
                FreeNode* head = reinterpret_cast<FreeNode*>(
                    oldPacked & 0x0000FFFFFFFFFFFFULL);

                if (head == nullptr) return nullptr;  // 空

                // ① 设置风险指针："我正在读这个节点"
                HP::protect(0, head);

                // ② 二次确认：head 是否还是同一个
                uint64_t verifyPacked = headPacked.load(std::memory_order_acquire);
                FreeNode* verifyHead = reinterpret_cast<FreeNode*>(
                    verifyPacked & 0x0000FFFFFFFFFFFFULL);

                if (head != verifyHead) {
                    HP::clear(0);
                    oldPacked = verifyPacked;
                    continue;  // head 变了，重试
                }

                // ③ CAS 弹出，tag 递增（防 ABA）
                uint16_t oldTag = static_cast<uint16_t>(oldPacked >> 48);
                uint64_t nextPtr = reinterpret_cast<uint64_t>(head->next);
                uint64_t newPacked = nextPtr | (static_cast<uint64_t>(oldTag + 1) << 48);

                if (headPacked.compare_exchange_weak(
                        oldPacked, newPacked,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    HP::clear(0);
                    return head;
                }

                HP::clear(0);
                // CAS 失败 → oldPacked 已更新，重试
            }
        }

        bool empty() const {
            return (headPacked.load(std::memory_order_acquire) & 0x0000FFFFFFFFFFFFULL) == 0;
        }
    };

    // -------------------------------------------------------
    // 线程本地缓存：批量取/还，减少 CAS 竞争
    // -------------------------------------------------------
    struct ThreadCache {
        std::array<std::vector<FreeNode*>, kNumClasses> caches;

        ThreadCache() = default;
    };

    // -------------------------------------------------------
    // 公开接口
    // -------------------------------------------------------

    LFMemoryPool() = default;
    ~LFMemoryPool() = default;

    // 分配 size 字节
    void* allocate(size_t size) {
        if (size > (1u << kMaxClass)) {
            // 超大块 → fallback 到 malloc
            return std::malloc(size);
        }

        int cls = sizeClass(size);
        ThreadCache& cache = getThreadCache();

        // ① 先从 ThreadCache 取（无锁）
        auto& localVec = cache.caches[cls];
        if (!localVec.empty()) {
            FreeNode* node = localVec.back();
            localVec.pop_back();
            return node;
        }

        // ② ThreadCache 空 → 批量从 FreeList 取
        for (int i = 0; i < kCacheCapacity; ++i) {
            FreeNode* node = freeLists_[cls].pop();
            if (node == nullptr) break;
            localVec.push_back(node);
        }

        if (!localVec.empty()) {
            FreeNode* node = localVec.back();
            localVec.pop_back();
            return node;
        }

        // ③ FreeList 也空 → 向操作系统申请
        return std::malloc(blockSize(cls));
    }

    // 释放 ptr（必须由 allocate 分配）
    void deallocate(void* ptr, size_t size) {
        if (size > (1u << kMaxClass)) {
            std::free(ptr);
            return;
        }

        int cls = sizeClass(size);
        ThreadCache& cache = getThreadCache();
        auto& localVec = cache.caches[cls];

        FreeNode* node = static_cast<FreeNode*>(ptr);

        // ① 还到 ThreadCache
        localVec.push_back(node);

        // ② ThreadCache 满了 → 批量还到 FreeList
        if (localVec.size() >= kCacheCapacity * 2) {
            size_t drain = localVec.size() / 2;
            for (size_t i = 0; i < drain; ++i) {
                FreeNode* n = localVec.back();
                localVec.pop_back();
                freeLists_[cls].push(n);
            }
        }
    }

    // 全局清理（进程退出前调用）
    void cleanup() {
        for (int cls = 0; cls < kNumClasses; ++cls) {
            FreeNode* node;
            while ((node = freeLists_[cls].pop()) != nullptr) {
                std::free(node);
            }
        }
    }

private:
    // 计算 size 对应的分档索引
    static int sizeClass(size_t size) {
        assert(size > 0);

        // 向上取 2 的幂次
        size_t aligned = 1;
        int cls = 0;
        while (aligned < size) {
            aligned <<= 1;
            cls++;
        }

        // clamp 到合法范围
        if (cls < kMinClass) cls = kMinClass;
        if (cls > kMaxClass) cls = kMaxClass;

        return cls - kMinClass;
    }

    // 分档对应的实际块大小
    static size_t blockSize(int cls) {
        return 1ull << (cls + kMinClass);
    }

    // 获取当前线程的 ThreadCache
    static ThreadCache& getThreadCache() {
        thread_local ThreadCache cache;
        return cache;
    }

    // 每档一个 FreeList
    std::array<FreeList, kNumClasses> freeLists_;
};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_LFMEMORYPOOL_H