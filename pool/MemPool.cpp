#include "MemPool.h"

#include <cstdlib>
#include <cstring>

namespace new_muduo {
    namespace pool {

        MemPool::MemPool()
            : startFree_(nullptr),
            endFree_(nullptr),
            heapSize_(0),
            totalAllocated_(0),
            totalFreed_(0) {
            for (auto& p : freeList_) {
                p = nullptr;
            }
        }

        MemPool& MemPool::instance() {
            static MemPool pool;
            return pool;
        }

        void* MemPool::refill(size_t n) {
            int nobjs = 20;
            char* chunk = chunkAlloc(n, nobjs);
            if (nobjs == 1) {
                return chunk;
            }

            FreeObj* result = reinterpret_cast<FreeObj*>(chunk);
            FreeObj* current = reinterpret_cast<FreeObj*>(chunk + n);
            size_t idx = freeListIndex(n);
            freeList_[idx] = current;

            for (int i = 1; i < nobjs - 1; ++i) {
                current->next = reinterpret_cast<FreeObj*>(reinterpret_cast<char*>(current) + n);
                current = current->next;
            }
            current->next = nullptr;
            return result;
        }

        char* MemPool::chunkAlloc(size_t size, int& nobjs) {
            char* result;
            size_t totalBytes = size * nobjs;
            size_t bytesLeft = static_cast<size_t>(endFree_ - startFree_);

            if (bytesLeft >= totalBytes) {
                result = startFree_;
                startFree_ += totalBytes;
                return result;
            }
            if (bytesLeft >= size) {
                nobjs = static_cast<int>(bytesLeft / size);
                totalBytes = size * nobjs;
                result = startFree_;
                startFree_ += totalBytes;
                return result;
            }

            size_t bytesToGet = 2 * totalBytes + roundUp(heapSize_ >> 4);
            if (bytesLeft > 0) {
                FreeObj* victim = reinterpret_cast<FreeObj*>(startFree_);
                size_t idx = freeListIndex(bytesLeft);
                victim->next = freeList_[idx];
                freeList_[idx] = victim;
            }

            startFree_ = static_cast<char*>(::operator new(bytesToGet));
            heapSize_ += bytesToGet;
            endFree_ = startFree_ + bytesToGet;

            return chunkAlloc(size, nobjs);
        }

    }  // namespace pool
}  // namespace neo