#ifndef MUDUO_URING_AWAITER_H
#define MUDUO_URING_AWAITER_H


#include <sys/socket.h>
#include <netinet/in.h>
#include <coroutine>
#include"uring_poller.h"

namespace new_muduo {
namespace io_uring {

class UringPoller; 


struct ReadAwaiter {
    //UringPoller*     poller;
    ReadAwaiter(int fd, void* buf, unsigned len, UringContext& ctx) :fd_(fd), buf_(buf), len_(len), ctx_(&ctx){}
    int              fd_;
    void*            buf_;
    unsigned         len_;
    size_t has_write = 0;
    UringContext* ctx_;         

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h);

    int await_resume() noexcept {
        return ctx_->res;
    }
};

struct WriteAwaiter {
    WriteAwaiter(int fd, const void* buf, unsigned len, UringContext& ctx) :fd_(fd), buf_(buf), len_(len), ctx_(&ctx) {}
    int              fd_;
    const void*      buf_;
    unsigned         len_;
    UringContext*  ctx_;

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h);

    int await_resume() noexcept {
        return ctx_->res;
    }
};


struct AcceptAwaiter {
    AcceptAwaiter(int fd, sockaddr_in& addr, socklen_t& len, UringContext& ctx):listen_fd_(fd),addr_(&addr),addrlen_(&len),ctx_(&ctx){}
    int              listen_fd_;
    sockaddr_in*     addr_;
    socklen_t*        addrlen_;
    UringContext*  ctx_;

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h);

    int await_resume() noexcept {
        return ctx_->res;          
    }
};



}
} 

#endif
