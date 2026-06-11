#include "Awaitables.h"
#include "CoroEventLoop.h"
#include"../net/EventLoop.h"

#include <unistd.h>
#include <sys/socket.h>

namespace new_muduo{
	using namespace coro;
	void AsyncRead::await_suspend(std::coroutine_handle<> h)
	{
		handle_ = h;
		CoroEventLoop* evLoop = CoroEventLoop::current();
		channel_ = std::make_unique<Channel>(evLoop->getRawLoop(), fd_);
		channel_->setReadCallback([this](Timestamp) {onReadable(); });
		channel_->enableReading();
	}

	void AsyncRead::onReadable()
	{
		bytesRead_ = ::read(fd_, buf_, count_);
		if (bytesRead_ < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
			return;
		}
		channel_->disableAll();
		channel_->remove();
		handle_.resume();
	}

	void AsyncWrite::await_suspend(std::coroutine_handle<> h) {
		handle_ = h;
		CoroEventLoop* evLoop = CoroEventLoop::current();
		channel_ = std::make_unique<Channel>(evLoop->getRawLoop(), fd_);
		channel_->setWriteCallback([this]() { onWritable(); });
		channel_->enableWriting();
	}

	void AsyncWrite::onWritable() {
		bytesWritten_ = ::write(fd_, buf_, count_);
		if (bytesWritten_ < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return;
		}
		channel_->disableAll();
		channel_->remove();
		handle_.resume();
	}

	void AsyncSleep::await_suspend(std::coroutine_handle<> h) {
		CoroEventLoop* evLoop = CoroEventLoop::current();
		evLoop->getRawLoop()->runAfter(seconds_, [h]() mutable {
			h.resume();
		});
	}

}