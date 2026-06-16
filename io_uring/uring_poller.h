#ifndef MUDUO_URING_POLLER_H
#define MUDUO_URING_POLLER_H


#include <liburing.h>
#include <atomic>
#include <functional>
#include <queue>
#include <mutex>
#include <memory>
#include "../net/Poller.h"
#include <coroutine>



namespace new_muduo {
namespace io_uring {

	class UringTcpConnection;
	class UringEventLoop;
	struct UringContext {
		std::coroutine_handle<> handle_;
		EventLoop* loop_;
		ssize_t res;
		UringContext(EventLoop* loop, ssize_t r = -1):loop_(loop), res(r){}
	};

class UringPoller:public Poller{
public:
    UringPoller(UringEventLoop* loop);
	~UringPoller() ;
	Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;

	bool submitRead(int fd, void* buf, size_t count, UringContext* user_data,off_t offset = 0 );
	bool submitWrite(int fd, const void* buf, size_t count, UringContext* user_data,off_t offset = 0);
	bool submitAccept(int fd, struct sockaddr* addr, UringContext* user_data, socklen_t* addrlen);
	static UringPoller* newPoller(UringEventLoop* loop);
	 void updateChannel(Channel* channel){}
	 void removeChannel(Channel* channel) {}
	//bool submitConnect(int fd, const struct sockaddr* addr, UringContext* user_data,socklen_t addrlen);

private:
	UringEventLoop* loop_;
	bool initUring(size_t entries = 1024);
	void handleCompletions();
	
public:
	void submitPendingSqes();
	void submitIfNeeded();
	void registerWakeupPoll();
private:
    struct io_uring_sqe* getSqe();
private:
	const size_t min_submit_sqes_ = 4 ;
    struct io_uring uring_;
    bool initialized_;
    bool wakeupPollArmed_ = false;
    std::atomic<int> pending_sqes_;
    std::vector<struct io_uring_cqe*> active_cqes_;
};

}
} 

#endif 
