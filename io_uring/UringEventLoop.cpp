#include "uring_poller.h"
#include "UringEventLoop.h"

namespace new_muduo::io_uring {

UringEventLoop::UringEventLoop()
    : UringPoller_(UringPoller::newPoller(this))
{}
UringEventLoop::~UringEventLoop() {}

void UringEventLoop::loop()
{
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;
    LOG_TRACE << "EventLoop " << this << " start looping";

    while (!quit_) {
        pollReturnTime_ = UringPoller_->poll(1000, &activeChannels_);
        doPendingFunctors();
    }

    LOG_TRACE << "EventLoop " << this << " stop looping";
    looping_ = false;
}

void UringEventLoop::wait_read(int fd, void* buf, signed len, UringContext* ctx) {
    UringPoller_->submitRead(fd, buf, len, ctx);
}

void UringEventLoop::wait_write(int fd, const void* buf, signed len, UringContext* ctx) {
    UringPoller_->submitWrite(fd, buf, len, ctx);
}

void UringEventLoop::wait_accept(int fd, sockaddr* peeraddr, UringContext* ctx, socklen_t* len) {
    UringPoller_->submitAccept(fd, peeraddr, ctx, len);
}

} // namespace new_muduo::io_uring
