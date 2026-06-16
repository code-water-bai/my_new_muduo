#include "uring_poller.h"
#include "UringEventLoop.h"
#include "awaiter.h"
#include "../base/Logging.h"
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <cassert>
#include <iostream>
#include "../base/Timestamp.h"


new_muduo::io_uring::UringPoller::UringPoller(UringEventLoop* loop):
	Poller(loop),
	loop_(dynamic_cast<UringEventLoop*>(loop)), 
	initialized_(false), 
	pending_sqes_(0)
{
	if (!initUring()) {
		LOG_SYSFATAL << "Failed to initialize io_uring";
	}

}

new_muduo::io_uring::UringPoller::~UringPoller()
{
	if (initialized_) {
		io_uring_queue_exit(&uring_);
	}
}

new_muduo::Timestamp new_muduo::io_uring::UringPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
	if (!initialized_) {
		LOG_ERROR << "io_uring not initialized";
		return Timestamp::now();
	}

	unsigned to_submit = pending_sqes_.load(std::memory_order_relaxed);
	if (to_submit > 0) {
		int ret = io_uring_submit(&uring_);
		if (ret > 0) {
			pending_sqes_ -= ret;
		}
	}

	struct io_uring_cqe* cqe;
	int ret;

	if (timeoutMs >= 0) {
		struct __kernel_timespec ts;
		ts.tv_sec = timeoutMs / 1000;
		ts.tv_nsec = (timeoutMs % 1000) * 1000000;

		ret = io_uring_wait_cqe_timeout(&uring_, &cqe, &ts);
	}
	else {
		ret = io_uring_wait_cqe(&uring_, &cqe);
	}

	if (ret < 0 && ret != -ETIME) {
		LOG_ERROR << "io_uring_wait_cqe failed: " << strerror(-ret);
		return Timestamp::now();
	}
	unsigned head;
	unsigned count = 0;
	io_uring_for_each_cqe(&uring_, head, cqe) {
		active_cqes_.push_back(cqe);
		count++;
	}

	if (count > 0) {
		io_uring_cq_advance(&uring_, count);
		handleCompletions();
		active_cqes_.clear();
	}

	return Timestamp::now();
}

bool new_muduo::io_uring::UringPoller::submitRead(int fd, void* buf, size_t count, UringContext* user_data, off_t offset)
{
	struct io_uring_sqe* sqe = getSqe();
	if (!sqe) return false;

	io_uring_prep_read(sqe, fd, buf, count, offset);
	io_uring_sqe_set_data(sqe, user_data);
	pending_sqes_++;
	return true;
}

bool new_muduo::io_uring::UringPoller::submitWrite(int fd, const void* buf, size_t count, UringContext* user_data, off_t offset)
{
	struct io_uring_sqe* sqe = getSqe();
	if (!sqe) return false;

	io_uring_prep_write(sqe, fd, buf, count, offset);
	io_uring_sqe_set_data(sqe, user_data);
	pending_sqes_++;
	return true;
	
}

bool new_muduo::io_uring::UringPoller::submitAccept(int fd, sockaddr* addr, UringContext* user_data, socklen_t* addrlen)
{
	struct io_uring_sqe* sqe = getSqe();
	if (!sqe) return false;

	io_uring_prep_accept(sqe, fd, addr, addrlen,0);
	io_uring_sqe_set_data(sqe, user_data);
	pending_sqes_++;
	return true;
}

new_muduo::io_uring::UringPoller* new_muduo::io_uring::UringPoller::newPoller(UringEventLoop* loop)
{
	return new UringPoller(loop);
}

bool new_muduo::io_uring::UringPoller::initUring(size_t entries)
{
	int ret = io_uring_queue_init(entries, &uring_, 0);
	if (ret < 0) {
		LOG_ERROR << "io_uring_queue_init failed: " << strerror(-ret);
		return false;
	}

	initialized_ = true;
	LOG_INFO << "io_uring initialized with " << entries << " entries";
	registerWakeupPoll();
	return true;
}

void new_muduo::io_uring::UringPoller::registerWakeupPoll()
{
	int fd = loop_->wakeupFd();
	struct io_uring_sqe* sqe = getSqe();
	if (!sqe) {
		LOG_ERROR << "UringPoller: failed to get SQE for wakeup poll";
		return;
	}
	io_uring_prep_poll_add(sqe, fd, POLLIN);
	io_uring_sqe_set_data(sqe, nullptr);  // nullptr marks wakeup CQE
	pending_sqes_++;
	wakeupPollArmed_ = true;
	submitPendingSqes();
}

void new_muduo::io_uring::UringPoller::handleCompletions()
{
	for (auto* cqe : active_cqes_) {
		UringContext* user_data = static_cast<UringContext*>(io_uring_cqe_get_data(cqe));
		if (user_data == nullptr) {
			// wakeup event: read eventfd to clear, then re-arm poll
			uint64_t dummy;
			::read(loop_->wakeupFd(), &dummy, sizeof(dummy));
			wakeupPollArmed_ = false;
			registerWakeupPoll();
			continue;
		}
		UringEventLoop* to_loop = dynamic_cast<UringEventLoop*>(user_data->loop_);
		user_data->res = cqe->res;
		if (!loop_->isInLoopThread()) {
			loop_->queueInLoop([user_data]() {user_data->handle_.resume(); });
		}
		else {
			user_data->handle_.resume();
		}
	}
}

void new_muduo::io_uring::UringPoller::submitPendingSqes()
{
	int pending = pending_sqes_.load();
	if (pending > 0) {
		int ret = io_uring_submit(&uring_);
		if (ret < 0) {
			LOG_ERROR << "io_uring_submit failed: " << strerror(-ret);
		}
		else {
			pending_sqes_ -= ret;
		}
	}
}

void new_muduo::io_uring::UringPoller::submitIfNeeded()
{
	if (pending_sqes_.load(std::memory_order_relaxed) >= min_submit_sqes_) {
		submitPendingSqes();
	}
}

io_uring_sqe* new_muduo::io_uring::UringPoller::getSqe()
{
	io_uring_sqe* sqe = io_uring_get_sqe(&uring_);
	if (sqe == nullptr) {
		submitPendingSqes();
		sqe = io_uring_get_sqe(&uring_);
	}
	return sqe;
}






