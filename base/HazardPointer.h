#ifndef NEO_MUDUO_BASE_HAZARDPOINTER_H
#define NEO_MUDUO_BASE_HAZARDPOINTER_H

#include <atomic>
#include <vector>
#include <array>
#include <cstdint>
#include <cstddef>

namespace new_muduo {

// ============================================================
// 风险指针（Hazard Pointer）
//
// 核心思想：线程在访问一个可能被其他线程删除的节点前，
// 先把自己的 HP 指向该节点 → 删除方扫描所有 HP，发现有人
// 正在用时就不删 → 延迟回收。
//
// Reference: Maged Michael, "Hazard Pointers: Safe Memory
//            Reclamation for Lock-Free Objects", IEEE TPDS 2004
// ============================================================

template <typename T>
class HazardPointerDomain {
public:
    // 每个线程持有的风险指针数量（通常 2 个：一个保护当前节点，一个保护 next）
    static constexpr int kMaxHazardPointers = 2;
    // 每个线程的退役链表长度达到此阈值时触发全局扫描
    static constexpr size_t kRetireThreshold = 64;
    // 最多支持 128 个线程
    static constexpr int kMaxThreads = 128;

    // -------------------------------------------------------
    // 线程注册的风险指针槽位
    // -------------------------------------------------------
    struct ThreadRecord {
        // 风险指针数组：线程声明"我正在访问这些对象"
        std::array<std::atomic<T*>, kMaxHazardPointers> hazardPointers;
        // 退役链表：待回收的节点
        std::vector<T*> retireList;
        // 活跃标记
        std::atomic<bool> active;

        ThreadRecord() {
            for (auto& hp : hazardPointers) {
                hp.store(nullptr, std::memory_order_relaxed);
            }
            active.store(false, std::memory_order_relaxed);
        }
    };

   /* struct ThreadRecord {
        std::array<std::atomic<T*>, kMaxHazardPointers> hazardPointers;
        std::vector<T*> retireList;
        std::atomic<bool> active;

        ThreadRecord() {
            for (auto& hp : hazardPointers) {
                hp.store(nullptr, std::memory_order_relaxed);
            }
            active.store(false, std::memory_order_relaxed);
        }
    };*/

    // -------------------------------------------------------
    // 获取当前线程的 ThreadRecord
    // -------------------------------------------------------
    static ThreadRecord& getThreadRecord() {
        thread_local ThreadRecord* thr = nullptr;
        if (thr == nullptr) {
            thr = registerThread();
        }
        return *thr;
    }

    /*static ThreadRecord& getThreadRecord() {
        ThreadRecord* ptr = nullptr;
        if (ptr == nullptr) {
            ptr = registerThread();
        }
        return &ptr;
    }*/


    // -------------------------------------------------------
    // 保护一个指针：声明 "我正在用这个对象"
    // idx: 0 or 1（用哪个槽位）
    // -------------------------------------------------------
    static void protect(int idx, T* ptr) {
        ThreadRecord& thr = getThreadRecord();
        thr.hazardPointers[idx].store(ptr, std::memory_order_seq_cst);
    }

    // -------------------------------------------------------
    // 清除保护
    // -------------------------------------------------------
    static void clear(int idx) {
        ThreadRecord& thr = getThreadRecord();
        thr.hazardPointers[idx].store(nullptr, std::memory_order_seq_cst);
    }

    // -------------------------------------------------------
    // 退役一个节点：延迟回收
    // 内部会批量触发扫描
    // -------------------------------------------------------
    static void retire(T* ptr) {
        ThreadRecord& thr = getThreadRecord();
        thr.retireList.push_back(ptr);

        if (thr.retireList.size() >= kRetireThreshold) {
            scan(thr);
        }
    }

    // -------------------------------------------------------
    // 强制回收：stop 时调用，回收所有退役节点
    // -------------------------------------------------------
    static void reclaimAll() {
        ThreadRecord& thr = getThreadRecord();
        scan(thr);

        // 扫描后 retireList 中剩余的是仍在被引用的节点，直接强制释放
        // （假定此时所有线程已停止）
        for (T* ptr : thr.retireList) {
            delete ptr;
        }
        thr.retireList.clear();
    }

private:
    // -------------------------------------------------------
    // 全局线程记录表 + 注册
    // -------------------------------------------------------
    static ThreadRecord* registerThread() {
        int slot = nextSlot_.fetch_add(1, std::memory_order_relaxed);
        if (static_cast<size_t>(slot) >= kMaxThreads) {
            // 超出上限，回退到简单模式（不过在实际使用中极少发生）
            abort();
        }
        records_[slot].active.store(true, std::memory_order_release);
        return &records_[slot];
    }

    // -------------------------------------------------------
    // 全局扫描：收集所有活跃线程的 HP，释放无人引用的退役节点
    // -------------------------------------------------------
    static void scan(ThreadRecord& thr) {
        // ① 收集所有活跃线程的风险指针
        std::vector<T*> hazardous;
        hazardous.reserve(kMaxThreads * kMaxHazardPointers);

        for (int i = 0; i < kMaxThreads; ++i) {
            if (!records_[i].active.load(std::memory_order_acquire))
                continue;

            for (int j = 0; j < kMaxHazardPointers; ++j) {
                T* hp = records_[i].hazardPointers[j].load(std::memory_order_seq_cst);
                if (hp != nullptr) {
                    hazardous.push_back(hp);
                }
            }
        }

        // ② 遍历退役链表，释放不危险的节点
        auto it = thr.retireList.begin();
        while (it != thr.retireList.end()) {
            if (isHazardous(*it, hazardous)) {
                ++it;  // 有人还在用，保留
            } else {
                delete *it;
                it = thr.retireList.erase(it);  // 安全释放
            }
        }
    }

    // 检查 ptr 是否在任何 HP 中
    static bool isHazardous(T* ptr, const std::vector<T*>& hazardous) {
        for (T* hp : hazardous) {
            if (hp == ptr) return true;
        }
        return false;
    }

    static std::array<ThreadRecord, kMaxThreads> records_;
    static std::atomic<int> nextSlot_;
};

// 静态成员定义
template <typename T>
std::array<typename HazardPointerDomain<T>::ThreadRecord,
           HazardPointerDomain<T>::kMaxThreads>
    HazardPointerDomain<T>::records_;

template <typename T>
std::atomic<int> HazardPointerDomain<T>::nextSlot_{0};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_HAZARDPOINTER_H