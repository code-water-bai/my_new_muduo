#pragma once

#include "../base/noncopyable.h"
#include "FixedBlockPool.h"
#include "SlabAllocator.h"

#include <cstddef>
#include <cstdint>

namespace new_muduo {
    namespace pool {

        // ============================================================
        // CoroutinePool - 协程帧专用内存池
        // 根据协程帧大小自动选择 slab 级别，大帧回退到全局分配
        // ============================================================
        class CoroutinePool : noncopyable {
        public:
            static CoroutinePool& instance();

            void* allocate(size_t size);
            void deallocate(void* ptr, size_t size);

            // 统计信息
            size_t slab256Alloc() const { return slab256_.allocatedCount(); }
            size_t slab512Alloc() const { return slab512_.allocatedCount(); }
            size_t slab1kAlloc() const { return slab1k_.allocatedCount(); }
            size_t slab2kAlloc() const { return slab2k_.allocatedCount(); }
            size_t slab4kAlloc() const { return slab4k_.allocatedCount(); }
            size_t globAlloc() const { return globAlloc_.load(); }
            size_t globFree() const { return globFree_.load(); }

        private:
            CoroutinePool() = default;

            FixedBlockPool<256>  slab256_;
            FixedBlockPool<512>  slab512_;
            FixedBlockPool<1024> slab1k_;
            FixedBlockPool<2048> slab2k_;
            FixedBlockPool<4096> slab4k_;

            std::atomic<size_t> globAlloc_{ 0 };
            std::atomic<size_t> globFree_{ 0 };
        };

        inline CoroutinePool& CoroutinePool::instance() {
            static CoroutinePool pool;
            return pool;
        }

        inline void* CoroutinePool::allocate(size_t size) {
            if (size <= 256)  return slab256_.allocate();
            if (size <= 512)  return slab512_.allocate();
            if (size <= 1024) return slab1k_.allocate();
            if (size <= 2048) return slab2k_.allocate();
            if (size <= 4096) return slab4k_.allocate();
            globAlloc_.fetch_add(1, std::memory_order_relaxed);
            return ::operator new(size);
        }

        inline void CoroutinePool::deallocate(void* ptr, size_t size) {
            if (size <= 256) { slab256_.deallocate(ptr); return; }
            if (size <= 512) { slab512_.deallocate(ptr); return; }
            if (size <= 1024) { slab1k_.deallocate(ptr);  return; }
            if (size <= 2048) { slab2k_.deallocate(ptr);  return; }
            if (size <= 4096) { slab4k_.deallocate(ptr);  return; }
            globFree_.fetch_add(1, std::memory_order_relaxed);
            ::operator delete(ptr);
        }

    }  // namespace pool
}  // namespace neo