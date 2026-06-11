#ifndef NEO_MUDUO_BASE_THREAD_H
#define NEO_MUDUO_BASE_THREAD_H

#include <functional>
#include <string>
#include <thread>
#include <memory>
#include <atomic>

#include "noncopyable.h"

namespace new_muduo {


class Thread : public noncopyable {
public:
    using ThreadFunc = std::function<void()>;

    explicit Thread(ThreadFunc func, const std::string& name = std::string());
    ~Thread();

    void start();
    void join();

    bool started() const { return started_; }
    bool joined() const { return joined_; }
    
    const std::string& name() const { return name_; }
    
    std::thread::id tid() const { return thread_->get_id(); }

    static Thread* currentThread();

    static void sleepUsec(int64_t usec);

private:
    void runInThread();

    ThreadFunc func_;
    std::string name_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> started_;
    std::atomic<bool> joined_;
};

namespace CurrentThread {
    extern thread_local int t_cachedTid;
    extern thread_local char t_tidString[32];
    extern thread_local int t_tidStringLength;
    extern thread_local const char* t_threadName;

    void cacheTid();

    inline int tid() {
        if (__builtin_expect(t_cachedTid == 0, 0)) {
            cacheTid();
        }
        return t_cachedTid;
    }

    inline const char* tidString() {
        return t_tidString;
    }

    inline int tidStringLength() {
        return t_tidStringLength;
    }

    inline const char* name() {
        return t_threadName;
    }

    bool isMainThread();
}

}  

#endif  