#include "UringCoroTcpConnection.h"
#include "uring_poller.h"
#include "UringEventLoop.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace new_muduo {
namespace io_uring {

	UringTcpConnection::UringTcpConnection(EventLoop* loop, int fd, const sockaddr_in* peer_addr, const std::string& name)
		:loop_(loop),
		fd_(fd),
		name_(name),
		peer_addr_(*peer_addr),
		state_(KDisconnected)
	{}

	UringTcpConnection::~UringTcpConnection()
	{
		if(fd_ > 0)::close(fd_);
	}

	void UringTcpConnection::start()
	{
		state_ = KConnecting;
		if (!handle_) {
			LOG_ERROR << "CoroTcpConnection::start - no handler set";
			forceclose();
			return;
		}

		connectionTask_ =  connectionLoop();
		if (connectionTask_) {
			connectionTask_.handle().resume();
		}
		
	}

	void UringTcpConnection::shutdown()
	{
		assert(state_ == KConnected);
		state_ = KDisconnecting;
		loop_->queueInLoop([this]() {shutdownInLoop(); });
	}

	void UringTcpConnection::forceclose()
	{
		if (state_ == KDisconnected || state_ == KDisconnecting) {
			return;
		}
		assert(state_ == KConnected || state_ == KConnecting);
		state_ = KDisconnecting;
		loop_->queueInLoop([this]() {forcecloseInLoop(); });
	}

	Task<ssize_t> UringTcpConnection::read(char* buf, signed len)
	{
		int remain_write = len;
		signed total = len;
		char* read_ = buf;
		UringContext ctx_(loop_);
		while (remain_write > 0) {

			co_await ReadAwaiter{ fd_,read_,static_cast<unsigned>(len),ctx_ };
			ssize_t res = ctx_.res;
			if (res > 0) {
				remain_write -= res;
				if (remain_write == 0) co_return total;
				read_ += res;
				len -= res;

			}else if (res == 0) {
				handleClose();
				co_return 0;
			}
			else {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
				LOG_SYSERR << "TcpConnection::handleRead";
				handleClose();
				co_return res;
			}
		}

	}

	Task<ssize_t> UringTcpConnection::send(const std::string& message)
	{
		return send(message.data(), message.size());
	}

	Task<ssize_t> UringTcpConnection::send(const char* data, int len)
	{
		UringContext ctx_(loop_);
		size_t remain_write = len;
		size_t total = len;
		const char* write_ = data;
		while (remain_write > 0) {
			co_await WriteAwaiter{fd_,write_,static_cast<unsigned>(len),ctx_ };
			ssize_t res = ctx_.res;
			if (res > 0) {
				remain_write -= res;
				if (remain_write == 0) co_return total;
				write_ += res;
				len -= res;
				continue;
			}
			else if (res == 0) {
				continue;
			}
			else  {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
				else {
					handleClose();
					co_return res;
				}
			}

		}
	}
	

	void UringTcpConnection::forcecloseInLoop()
	{
	   ::shutdown(fd_, SHUT_RDWR);
		handleClose();
	}

	void UringTcpConnection::shutdownInLoop()
	{
		::shutdown(fd_, SHUT_WR);
	}

	void UringTcpConnection::handleClose()
	{
		if (state_ == KDisconnected) return;

		UringTcpConnectionPtr guard = shared_from_this();
		closeCallback_(shared_from_this());
		state_ = KDisconnected;

		loop_->queueInLoop([guard = shared_from_this()]() mutable {

		});
	}

	Task<void> UringTcpConnection::connectionLoop() {
		state_ = KConnected;

		co_await handle_(shared_from_this());

		if (state_ != KDisconnected) {
			forceclose();
		}

		co_return;
	}





}
} 
