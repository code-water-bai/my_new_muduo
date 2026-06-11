// muduo TimerId 定时器标识
// 摘录自 muduo/net/TimerId.h

#ifndef MUDUO_NET_TIMERID_H
#define MUDUO_NET_TIMERID_H

#include "../base/noncopyable.h"
#include <cstdint>


namespace new_muduo
{
        class Timer;
        class TimerId 
        {
        public:
            TimerId()
                : timer_(nullptr),
                sequence_(0)
            {
            }

            TimerId(Timer* timer,int64_t seq)
                : timer_(timer),
                sequence_(seq)
            {
            }

            // default copy-ctor, dtor and assignment are okay

            friend class TimerQueue;

        private:
            Timer* timer_;       // 实际定时器对象的指针
            int64_t sequence_;   // 序列号，防止 ABA 问题
        };

}  // namespace muduo

#endif  // MUDUO_NET_TIMERID_H
