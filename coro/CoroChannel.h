#pragma once

#include "../base/noncopyable.h"
#include "../net/Channel.h"

#include <coroutine>
#include <functional>
#include <memory>
#define neo new_muduo
namespace new_muduo {
    class EventLoop;
    class Buffer;
    class Timestamp;
}  // namespace neo

namespace new_muduo::coro {

    class CoroEventLoop;

    // ============================================================
    // CoroChannel - 协程版 Channel
    // 在 I/O 事件就绪时恢复协程
    // ============================================================
    class CoroChannel : noncopyable {
    public:
        using ReadCallback = std::function<void(neo::Buffer*)>;
        using WriteCallback = std::function<void()>;
        using CloseCallback = std::function<void()>;

        CoroChannel(CoroEventLoop* loop, int fd);
        ~CoroChannel();

        int fd() const { return fd_; }

        void setReadCallback(ReadCallback cb) { readCallback_ = std::move(cb); }
        void setWriteCallback(WriteCallback cb) { writeCallback_ = std::move(cb); }
        void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

        void enableReading();
        void disableReading();
        void enableWriting();
        void disableWriting();
        void disableAll();
        void remove();

        bool isReading() const { return reading_; }
        bool isWriting() const { return writing_; }

        CoroEventLoop* getLoop() const { return loop_; }

        // 挂起当前协程等待可读事件
        void waitReadable(std::coroutine_handle<> h);
        void waitWritable(std::coroutine_handle<> h);

    private:
        void handleRead(neo::Timestamp t);
        void handleWrite();
        void handleClose();

        CoroEventLoop* loop_;
        int fd_;
        ReadCallback readCallback_;
        WriteCallback writeCallback_;
        CloseCallback closeCallback_;
        bool reading_;
        bool writing_;
        bool attached_;

        std::coroutine_handle<> readWaiter_;
        std::coroutine_handle<> writeWaiter_;

        std::unique_ptr<neo::Channel> channel_;
    };

}  // namespace neo::coro