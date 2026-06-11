#pragma once

#include "../base/noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <new>

namespace new_muduo {
    namespace pool {

        // ============================================================
        // FixedBlockPool<N> - 固定大小块内存池模板
        // N: 每个块的大小（字节）
        // ============================================================
        template <size_t N>
        class FixedBlockPool : noncopyable {
            static_assert(N >= sizeof(void*), "Block size must be at least sizeof(void*)");

            struct Node {
                Node* next;
            };

        public:
            FixedBlockPool()
                : freeList_(nullptr),
                allocated_(0),
                freed_(0) {
                allocateChunk();
            }

            ~FixedBlockPool() {
                for (auto* chunk : chunks_) {
                    ::operator delete(chunk);
                }
            }

            void* allocate() {
                if (freeList_ == nullptr) {
                    allocateChunk();
                }
                void* result = freeList_;
                freeList_ = freeList_->next;
                allocated_.fetch_add(1, std::memory_order_relaxed);
                return result;
            }

            void deallocate(void* ptr) {
                if (ptr == nullptr) return;
                auto* node = static_cast<Node*>(ptr);
                node->next = freeList_;
                freeList_ = node;
                freed_.fetch_add(1, std::memory_order_relaxed);
            }

            size_t allocatedCount() const { return allocated_.load(std::memory_order_relaxed); }
            size_t freedCount() const { return freed_.load(std::memory_order_relaxed); }

        private:
            static const size_t kChunkSize = 64;  // 每次分配 64 个块

            void allocateChunk() {
                char* mem = static_cast<char*>(::operator new(N * kChunkSize, std::align_val_t{ alignof(Node) }));
                chunks_.push_back(mem);

                for (size_t i = 0; i < kChunkSize; ++i) {
                    auto* node = reinterpret_cast<Node*>(mem + i * N);
                    node->next = freeList_;
                    freeList_ = node;
                }
            }

            Node* freeList_;
            std::vector<char*> chunks_;
            std::atomic<size_t> allocated_;
            std::atomic<size_t> freed_;
        };

    }  // namespace pool
}  // namespace neo