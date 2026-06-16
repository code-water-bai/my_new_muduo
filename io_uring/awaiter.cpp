#include"awaiter.h"
#include"UringEventLoop.h"
#include"uring_poller.h"

namespace new_muduo::io_uring {
	void ReadAwaiter::await_suspend(std::coroutine_handle<> h) {
		ctx_->handle_ = h;
		UringEventLoop* loop = dynamic_cast<UringEventLoop*>(ctx_->loop_);
		loop->wait_read(fd_, buf_, len_, ctx_);
		dynamic_cast<UringPoller*>(loop->poller())->submitIfNeeded();
	}

	void WriteAwaiter::await_suspend(std::coroutine_handle<> h) {
		ctx_->handle_ = h;
		UringEventLoop* loop = dynamic_cast<UringEventLoop*>(ctx_->loop_);
		loop->wait_write(fd_, buf_, len_, ctx_);
		dynamic_cast<UringPoller*>(loop->poller())->submitIfNeeded();
	}

	void AcceptAwaiter::await_suspend(std::coroutine_handle<> h)
	{
		ctx_->handle_ = h;
		UringEventLoop* loop = dynamic_cast<UringEventLoop*>(ctx_->loop_);
		loop->wait_accept(listen_fd_,(sockaddr*)addr_,ctx_, addrlen_);
		dynamic_cast<UringPoller*>(loop->poller())->submitIfNeeded();
	}

}