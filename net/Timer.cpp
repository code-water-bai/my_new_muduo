#include "./Timer.h"


namespace new_muduo{


	std::atomic_int_least64_t Timer::s_numCreated_{ 0 };

	void Timer::restart(Timestamp now)
	{
		if (repeat_) {
			expiration_ = addTime(expiration_, interval_);
		}
		else {
			expiration_ = Timestamp::invaild();
		}
	}
}