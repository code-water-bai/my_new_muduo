#include "TimerQueue.h"
#include "Timerld.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include "./EventLoop.h"
#include <cstring>
#include "Timer.h"
#include "../base/Timestamp.h"

using namespace new_muduo;

int createTimerfd() {
	int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
		TFD_NONBLOCK | TFD_CLOEXEC);
	return timerfd;
}

struct timespec howMuchTimeFromNow(Timestamp when)
{
	int64_t microseconds = when.microSecondsSinceEpoch()
		- Timestamp::now().microSecondsSinceEpoch();
	if (microseconds < 100)
	{
		microseconds = 100;
	}
	struct timespec ts;
	ts.tv_sec = static_cast<time_t>(
		microseconds / Timestamp::kMicroSecondsPerSecond);
	ts.tv_nsec = static_cast<long>(
		(microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
	return ts;
}

void readTimerfd(int timerfd, Timestamp now) {
	uint64_t buf;
	int n = ::read(timerfd, &buf, sizeof(buf));
}

void resetTimerfd(int timerfd, Timestamp expiration) {
	struct itimerspec newValue;
	struct itimerspec oldValue;
	memset(&newValue,0,sizeof newValue);
	memset(&oldValue,0, sizeof oldValue);

	newValue.it_value = howMuchTimeFromNow(expiration);
	int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
}
	
TimerQueue::TimerQueue(EventLoop* loop)
	: loop_(loop),
	timerfd_(createTimerfd()),
	timerfdChannel_(loop, timerfd_),
	timers_(),
	callingExpiredTimers_(false)
{
	timerfdChannel_.setReadCallback(
		std::bind(&TimerQueue::handleRead, this));
	timerfdChannel_.enableReading();
}

new_muduo::TimerQueue::~TimerQueue()
{
	timerfdChannel_.disableAll();
	timerfdChannel_.remove();
	::close(timerfd_);
	for (const Entry& timer : timers_)
	{
		delete timer.second;
	}
}

TimerId new_muduo::TimerQueue::addTimer(TimerCallback cb, Timestamp when, double interval)
{
	Timer* timer = new Timer(std::move(cb), when, interval);
	loop_->runInLoop(
		std::bind(&TimerQueue::addTimerInLoop, this, timer));
	return TimerId(timer, timer->sequence());
}

void new_muduo::TimerQueue::cancel(TimerId timerId)
{
	loop_->runInLoop(
		std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void new_muduo::TimerQueue::addTimerInLoop(Timer* timer)
{
	bool is_change = false;
	is_change = insert(timer);
	if (is_change) {
		resetTimerfd(timerfd_, timer->expiration());
	}
}

void new_muduo::TimerQueue::cancelInLoop(TimerId timerId)
{
	ActiveTimer cur(timerId.timer_, timerId.sequence_);
	auto it = activeTimers_.find(cur);
	if (it != activeTimers_.end()) {
		Entry new_cur(timerId.timer_->expiration(), timerId.timer_);
		timers_.erase(new_cur);
		activeTimers_.erase(it);
		delete cur.first;
	}
	else if (callingExpiredTimers_) {
		cancelingTimers_.insert(cur);
	}
}

void new_muduo::TimerQueue::handleRead()
{
	Timestamp now(Timestamp::now());
	readTimerfd(timerfd_, now);

	std::vector<Entry> expired = getExpired(now);
	callingExpiredTimers_ = true;
	cancelingTimers_.clear();

	for (auto& it : expired) {
		Timer* cur = it.second;
		cur->run();
	}

	callingExpiredTimers_ = false;
	reset(expired, now);

}

std::vector<TimerQueue::Entry> new_muduo::TimerQueue::getExpired(Timestamp now)
{
	std::vector<Entry> expired;
	Entry last(now, reinterpret_cast<Timer*>(UINTPTR_MAX));

	auto it = timers_.lower_bound(last);
	std::copy(timers_.begin(), it, back_inserter(expired));
	timers_.erase(timers_.begin(), it);

	for (const Entry& it : expired)
	{
		ActiveTimer timer(it.second, it.second->sequence());
		 activeTimers_.erase(timer);
	}

	return expired;
}

void new_muduo::TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
	Timestamp nextExpire;

	for (auto& cur : expired) {
		ActiveTimer rub(cur.second, cur.second->sequence());
		if (cur.second->repeat() && cancelingTimers_.find(rub) == cancelingTimers_.end()) {
			cur.second->restart(now);
			insert(cur.second);
		}
		else {
			delete cur.second;
		}
	}

	if (!timers_.empty())
	{
		nextExpire = timers_.begin()->second->expiration();
	}

	if (nextExpire.valid())
	{
		resetTimerfd(timerfd_, nextExpire);
	}
}

bool new_muduo::TimerQueue::insert(Timer* timer)
{
	bool is_change = false;
	Timestamp when = timer->expiration();
	auto it = timers_.begin();

	if (it == timers_.end() || when < it->first) {
		is_change = true;
	}

	timers_.insert({ when,timer });
	activeTimers_.insert({ timer,timer->sequence() });
	return is_change;
}


	
