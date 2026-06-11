#pragma once

#include "../base/noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <vector>
#include <new>
#include <mutex>
#include <algorithm>

namespace new_muduo {
    namespace pool {

        // ============================================================
        // SlabAllocator - Slab 分配器，支持多种固定大小的内存分配
        // 适合频繁分配/释放固定大小对象的场景
        // ============================================================
        class SlabAllocator : noncopyable {
        public:
            explicit SlabAllocator(size_t objectSize, size_t objectsPerSlab = 64)
                : objectSize_(std::max(objectSize, sizeof(void*))),
                objectsPerSlab_(objectsPerSlab),
                freeList_(nullptr) {
            }

            ~SlabAllocator() {
                for (auto* slab : slabs_) {
                    ::operator delete(slab);
                }
            }

            void* allocate() {
                if (freeList_ == nullptr) {
                    allocateSlab();
                }
                void* result = freeList_;
                freeList_ = *static_cast<void**>(freeList_);
                return result;
            }

            void deallocate(void* ptr) {
                if (ptr == nullptr) return;
                *static_cast<void**>(ptr) = freeList_;
                freeList_ = ptr;
            }

            size_t objectSize() const { return objectSize_; }

        private:
            void allocateSlab() {
                size_t slabSize = objectSize_ * objectsPerSlab_;
                char* mem = static_cast<char*>(::operator new(slabSize, std::align_val_t{ alignof(std::max_align_t) }));
                slabs_.push_back(mem);

                for (size_t i = 0; i < objectsPerSlab_; ++i) {
                    void* obj = mem + i * objectSize_;
                    deallocate(obj);
                }
            }

            size_t objectSize_;
            size_t objectsPerSlab_;
            void* freeList_;
            std::vector<char*> slabs_;
        };

    }  // namespace pool
}  // namespace neo