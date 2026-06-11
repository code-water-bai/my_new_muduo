#pragma once

#include "../base/noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <atomic>
#include <new>
#include <vector>
#include <memory>

namespace new_muduo {
    namespace pool {

        // ============================================================
        // MemPool - 通用可变大小内存池（自由链表 + 大块分配）
        // ============================================================
        class MemPool : noncopyable {
        public:
            static const size_t kDefaultBlockSize = 4096;
            static const size_t kMaxBytes = 256;
            static const size_t kAlign = 8;
            static const size_t kNumFreeLists = kMaxBytes / kAlign;

            static MemPool& instance();

            void* allocate(size_t n);
            void deallocate(void* p, size_t n);

            size_t totalAllocated() const { return totalAllocated_; }
            size_t totalFreed() const { return totalFreed_; }

        private:
            MemPool();

            void* refill(size_t n);
            char* chunkAlloc(size_t size, int& nobjs);

            union FreeObj {
                FreeObj* next;
                char clientData[1];
            };

            static size_t roundUp(size_t bytes) {
                return (bytes + kAlign - 1) & ~(kAlign - 1);
            }

            static size_t freeListIndex(size_t bytes) {
                return (bytes + kAlign - 1) / kAlign - 1;
            }

            FreeObj* freeList_[kNumFreeLists];
            char* startFree_;
            char* endFree_;
            size_t heapSize_;

            std::atomic<size_t> totalAllocated_;
            std::atomic<size_t> totalFreed_;
        };

        inline void* MemPool::allocate(size_t n) {
            if (n > kMaxBytes) {
                totalAllocated_ += n;
                return ::operator new(n);
            }
            size_t idx = freeListIndex(n);
            FreeObj* result = freeList_[idx];
            if (result == nullptr) {
                return refill(roundUp(n));
            }
            freeList_[idx] = result->next;
            totalAllocated_ += n;
            return result;
        }

        inline void MemPool::deallocate(void* p, size_t n) {
            if (n > kMaxBytes) {
                totalFreed_ += n;
                ::operator delete(p);
                return;
            }
            totalFreed_ += n;
            size_t idx = freeListIndex(n);
            auto* obj = static_cast<FreeObj*>(p);
            obj->next = freeList_[idx];
            freeList_[idx] = obj;
        }

    }  // namespace pool
}  // namespace neo