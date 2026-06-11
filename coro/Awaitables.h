#pragma once

#include "../net/Channel.h"
#include "../net/Buffer.h"
#include "../base/Timestamp.h"

#include <coroutine>
#include <sys/types.h>
#include <cstddef>


namespace new_muduo {
	namespace coro { class CoroEventLoop; }

	class AsyncRead {
	public:
		AsyncRead(int fd, char* buf, size_t count)
			: fd_(fd), buf_(buf), count_(count), bytesRead_(0) {}

		bool await_ready() const noexcept { return false; }

		void await_suspend(std::coroutine_handle<> h);
		ssize_t await_resume() const { return bytesRead_; }

		void onReadable();
	private:
		int fd_;
		char* buf_;
		size_t count_;
		ssize_t bytesRead_;
		std::coroutine_handle<> handle_;
		std::unique_ptr<Channel> channel_;
	};

	class AsyncWrite {
	public:
		AsyncWrite(int fd, const char* buf, size_t count)
			: fd_(fd), buf_(buf), count_(count), bytesWritten_(0) {}

		bool await_ready() const noexcept { return false; }

		void await_suspend(std::coroutine_handle<> h);
		ssize_t await_resume() const { return bytesWritten_; }

		void onWritable();

	private:
		int fd_;
		const char* buf_;
		size_t count_;
		ssize_t bytesWritten_;
		std::coroutine_handle<> handle_;
		std::unique_ptr<Channel> channel_;
	};

	class AsyncSleep {
	public:
		explicit AsyncSleep(double seconds) : seconds_(seconds) {}

		bool await_ready() const noexcept { return false; }

		void await_suspend(std::coroutine_handle<> h);
		void await_resume() const noexcept {}

	private:
		double seconds_;
	};


}