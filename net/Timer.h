
#ifndef MUDUO_NET_TIMER_H
#define MUDUO_NET_TIMER_H

#include "./Callbacks.h"
#include "../base/Timestamp.h"
#include <atomic>

 namespace new_muduo {
	 class Timer : public noncopyable {
	 public:
		 Timer(TimerCallback cb, Timestamp when, double interval)
			 : callback_(std::move(cb)),
			 expiration_(when),
			 interval_(interval),
			 repeat_(interval > 0.0),
			 sequence_(s_numCreated_.fetch_add(1))
		 {
		 }
		 void run() const
		 {
			 callback_();
		 }
		 Timestamp expiration() const { return expiration_; }
		 bool repeat() const { return repeat_; }
		 int64_t sequence() const { return sequence_; }
		 void restart(Timestamp now);
		 static int64_t numCreated() { return s_numCreated_.fetch_add(1); }


	 private:
		 const TimerCallback callback_;    // 定时器回调
		 Timestamp expiration_;            // 超时时间
		 const double interval_;           // 超时间隔（秒），>0 表示重复定时器
		 const bool repeat_;               // 是否重复
		 const int64_t sequence_;          // 全局唯一序列号

		 static std::atomic_int_least64_t s_numCreated_; //全局序列号
	};

	
}


#endif