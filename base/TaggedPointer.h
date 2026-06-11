#ifndef NEO_MUDUO_BASE_TAGGEDPOINTER_H
#define NEO_MUDUO_BASE_TAGGEDPOINTER_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <utility>

namespace new_muduo {

// ============================================================
// 标签化指针：把 48 位指针 + 16 位版本号打包成 64 位
// x86-64 用户态虚拟地址最高 16 位永远是符号扩展 → 可安全借用
// ============================================================
template <typename T>
class TaggedPointer {
    static constexpr uint64_t kPtrMask  = 0x0000FFFFFFFFFFFFULL;  // 低 48 位
    static constexpr uint64_t kTagMask  = 0xFFFF000000000000ULL;  // 高 16 位
    static constexpr int     kTagShift = 48;

public:
    TaggedPointer() : packed_(0) {}

    TaggedPointer(T* ptr, uint16_t tag = 0)
        : packed_(pack(ptr, tag)) {}

    // 获取原始指针（剥离 tag）
    T* ptr() const {
        return reinterpret_cast<T*>(packed_.load(std::memory_order_acquire) & kPtrMask);
    }

    // 获取版本号
    uint16_t tag() const {
        return static_cast<uint16_t>(
            packed_.load(std::memory_order_acquire) >> kTagShift);
    }

    // 原子 CAS
    bool compare_exchange(TaggedPointer& expected, T* newPtr, uint16_t newTag,
                          std::memory_order succ = std::memory_order_acq_rel,
                          std::memory_order fail = std::memory_order_acquire) {
        uint64_t oldPacked = expected.packed();
        uint64_t newPacked = pack(newPtr, newTag);
        return packed_.compare_exchange_strong(oldPacked, newPacked, succ, fail);
    }

    // 原子 CAS，同时更新 expected
    bool compare_exchange(TaggedPointer& expected, T* newPtr,
                          std::memory_order succ = std::memory_order_acq_rel,
                          std::memory_order fail = std::memory_order_acquire) {
        return compare_exchange(expected, newPtr, expected.tag() + 1, succ, fail);
    }

    // 原子 store
    void store(T* ptr, uint16_t tag,
               std::memory_order order = std::memory_order_release) {
        packed_.store(pack(ptr, tag), order);
    }

    // 非原子打包值
    uint64_t packed() const { return packed_.load(std::memory_order_relaxed); }

private:
    static uint64_t pack(T* ptr, uint16_t tag) {
        uint64_t addr = reinterpret_cast<uint64_t>(ptr);
        uint64_t signExt = (addr & (1ULL << 47)) ? kTagMask : 0;
        assert((addr & kTagMask) == signExt || addr == 0);
        return addr | (static_cast<uint64_t>(tag) << kTagShift);
    }

    /*static uint64_t pack(T* ptr, uint16_t tag) {
        uint64_t addr = reinterpret_cast<uint64_t>(ptr);
        uint64_t sign = (addr & (1ULL << 47)) ? KTagMask : 0;
        assert((addr & kTagMask) == sign || addr == 0); 
        return addr | (statci_cast<uint64_t>(tag) << kTagShift);

    }*/

    std::atomic<uint64_t> packed_;
};

template <typename T>
class LFStack {
    struct Node {
        T data;
        Node* next;

        template <typename... Args>
        Node(Args&&... args)
            : data(std::forward<Args>(args)...), next(nullptr) {}
    };

public:
    LFStack() = default;
    ~LFStack() {
        // 生产环境需要 hazard pointer 或 epoch reclamation
        // 这里简化：pop 所有节点
        T dummy;
        while (pop(dummy)) {}
    }

    // push：CAS head → 新节点（tag 不变，指针变）
    void push(const T& val) {
        Node* node = new Node(val);

        TaggedPointer<Node> oldHead = head_.load();
        TaggedPointer<Node> newHead;

        do {
            node->next = oldHead.ptr();
            // tag 不变，只改指针
            newHead = TaggedPointer<Node>(node, oldHead.tag());
        } while (!head_.compare_exchange(oldHead, newHead));
    }

   

    // pop：CAS head → head->next（tag 递增）
    bool pop(T& out) {
        TaggedPointer<Node> oldHead = head_.load();
        Node* node = nullptr;

        do {
            node = oldHead.ptr();
            if (node == nullptr) return false;  // 栈空

            // 读下一个节点
            Node* next = node->next;

            // tag + 1：即使节点地址被分配器复用，
            // 版本号不同 → CAS 必然失败
            TaggedPointer<Node> newHead(next, oldHead.tag() + 1);

            if (head_.compare_exchange(oldHead, newHead)) {
                out = node->data;
                delete node;
                return true;
            }
        } while (true);
    }

    /*bool pop(T& out) {
        TaggedPointer oldhead = head_.load();
        Node* node = nullptr;

        do {
            node = oldhead.ptr();
            if (node == nullptr) return false;

            Node* nxtnode = node->next;
            TaggedPointer newhead(nxtnode, oldhead.tag()+1);


            if (head_.compare_exchange(oldHead, newHead)) {
                out = node->data;
                delete node;
                return true;
            }

        } while (true)

    }*/

    bool empty() const { return head_.load().ptr() == nullptr; }

private:
    //std::atomic<uint64_t> headPacked_{0};

    //// 辅助：原子读/写 TaggedPointer
    //TaggedPointer<Node> loadHead() {
    //    TaggedPointer<Node> tp;
    //    tp.store(reinterpret_cast<Node*>(headPacked_.load(std::memory_order_acquire) & 0x0000FFFFFFFFFFFFULL),
    //             static_cast<uint16_t>(headPacked_.load(std::memory_order_acquire) >> 48));
    //    return tp;
    //}

    // 简化：直接操作裸 atomic<uint64_t>
    struct alignas(16) HeadProxy {
        TaggedPointer<Node> load() const {
            uint64_t p = packed.load(std::memory_order_acquire);
            return TaggedPointer<Node>(
                reinterpret_cast<Node*>(p & 0x0000FFFFFFFFFFFFULL),
                static_cast<uint16_t>(p >> 48));
        }

        bool compare_exchange(TaggedPointer<Node>& expected,
                             TaggedPointer<Node>& desired) {
            uint64_t old = expected.packed();
            return packed.compare_exchange_strong(
                old, desired.packed(),
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }

        std::atomic<uint64_t> packed{0};
    };

   /* struct alignas(16) HeadProxy {
        std::atomic<uint64_t> packed(0);

        TaggedPointer<Node> load() const {
            uint64_t p = packed.load(std::memory_order_acquire);
            return TaggedPointer<Node>(reinterpret_cast<Node*>(p & TaggedPointer::kPtrMask), static<uint16_t>(p >> TaggedPointer::KTagMask));
        }
    };*/

    HeadProxy head_;
};

}  

#endif  // NEO_MUDUO_BASE_TAGGEDPOINTER_H