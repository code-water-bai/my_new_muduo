#pragma once
#include "../net/EventLoop.h"
#include <sys/socket.h>
#include <assert.h>
#include "../base/Logging.h"
#include <memory>

class UringPoller;
struct UringContext;

namespace new_muduo::io_uring {
	class UringEventLoop : public EventLoop {
	public:
		UringEventLoop();
		~UringEventLoop();

		void loop() override;
		Poller* poller() const override { return UringPoller_.get(); }

	private:
		std::unique_ptr<UringPoller> UringPoller_;
	public:
		void wait_read(int fd, void* buf, signed len, UringContext* ctx);
		void wait_write(int fd, const void* buf, signed len, UringContext* ctx);
		void wait_accept(int fd, sockaddr* peeraddr, UringContext* ctx, socklen_t* len);
	};
}