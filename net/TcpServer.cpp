#include "TcpServer.h"
#include "TcpConnection.h"
#include "EventLoop.h"
#include "../base/Logging.h"
#include "Socket.h"

#include <functional>
#include <condition_variable>
#include <assert.h>

namespace new_muduo {
	namespace detail {
        class EventLoopThread : new_muduo::noncopyable {
        public:
            using ThreadInitCallback = std::function<void(EventLoop*)>;

            EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback())
                : loop_(nullptr),
                exiting_(false),
                thread_(std::bind(&EventLoopThread::threadFunc, this)),
                mutex_(),
                cond_(),
                callback_(cb) {
            }

            ~EventLoopThread() {
                exiting_ = true;
                if (loop_ != nullptr) {
                    loop_->quit();
                    thread_.join();
                }
            }

            EventLoop* startLoop() {
                assert(!thread_.started());
                thread_.start();

                EventLoop* loop = nullptr;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cond_.wait(lock, [this] { return loop_ != nullptr; });
                    loop = loop_;
                }
                return loop;
            }

        private:
            void threadFunc() {
                EventLoop loop;
                if (callback_) {
                    callback_(&loop);
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    loop_ = &loop;
                    cond_.notify_one();
                }
                loop.loop();
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = nullptr;
            }

            EventLoop* loop_;
            bool exiting_;
            Thread thread_;
            std::mutex mutex_;
            std::condition_variable cond_;
            ThreadInitCallback callback_;
        };

        class EventLoopThreadPool : new_muduo::noncopyable {
        public:
            using ThreadInitCallback = std::function<void(EventLoop*)>;

            EventLoopThreadPool(EventLoop* baseLoop, const std::string& name)
                : baseLoop_(baseLoop),
                name_(name),
                started_(false),
                numThreads_(0),
                next_(0) {
            }

            ~EventLoopThreadPool() = default;

            void setThreadNum(int numThreads) { numThreads_ = numThreads; }

            void start(const ThreadInitCallback& cb) {
                assert(!started_);
                baseLoop_->assertInLoopThread();
                started_ = true;

                for (int i = 0; i < numThreads_; ++i) {
                    char buf[name_.size() + 32];
                    snprintf(buf, sizeof(buf), "%s%d", name_.c_str(), i);
                    auto* t = new EventLoopThread(cb);
                    threads_.push_back(std::unique_ptr<EventLoopThread>(t));
                    loops_.push_back(t->startLoop());
                }

                if (numThreads_ == 0 && cb) {
                    cb(baseLoop_);
                }
            }

            EventLoop* getNextLoop() {
                baseLoop_->assertInLoopThread();
                EventLoop* loop = baseLoop_;

                if (!loops_.empty()) {
                    loop = loops_[next_];
                    ++next_;
                    if (static_cast<size_t>(next_) >= loops_.size()) {
                        next_ = 0;
                    }
                }
                return loop;
            }

            std::vector<EventLoop*> getAllLoops() {
                if (loops_.empty()) {
                    return { baseLoop_ };
                }
                return loops_;
            }

        private:
            EventLoop* baseLoop_;
            std::string name_;
            bool started_;
            int numThreads_;
            int next_;
            std::vector<std::unique_ptr<EventLoopThread>> threads_;
            std::vector<EventLoop*> loops_;
        };

	}

    using EventLoopThreadPool = new_muduo::detail::EventLoopThreadPool;
    TcpServer::TcpServer(EventLoop* loop,
        const InetAddress& listenAddr,
        const std::string& name,
        bool reusePort)
        : loop_(loop),
        ipPort_(listenAddr.toIpPort()),
        name_(name),
        acceptor_(std::make_unique<Acceptor>(loop, listenAddr, reusePort)),
        threadPool_(std::make_shared<EventLoopThreadPool>(loop, name)),
        started_(0),
        nextConnId_(1) {
        acceptor_->setNewConnectionCallback([this](int sockfd, const InetAddress& peerAddr) {
            newConnection(sockfd, peerAddr);
            });
    }

    void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr) {
        loop_->assertInLoopThread();
        char buf[64];
        snprintf(buf, sizeof(buf), "#%d", nextConnId_);
        ++nextConnId_;
        std::string connName = name_ + buf;

        LOG_TRACE << "TcpServer::newConnection [" << name_
            << "] - new connection [" << connName
            << "] from " << peerAddr.toIpPort();

        InetAddress localAddr(getLocalAddr(sockfd));
        EventLoop* ioLoop = threadPool_->getNextLoop();
        TcpConnectionPtr conn = std::make_shared<TcpConnection>(
            ioLoop, connName, sockfd, localAddr, peerAddr);

        connections_[connName] = conn;
        conn->setConnectionCallback(connectionCallback_);
        conn->setMessageCallback(messageCallback_);
        conn->setWriteCompleteCallback(writeCompleteCallback_);
        conn->setCloseCallback([this](const TcpConnectionPtr& c) { removeConnection(c); });

        ioLoop->runInLoop([conn]() { conn->connectEstablished(); });
    }

    void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
        loop_->runInLoop([this, conn]() { removeConnectionInLoop(conn); });
    }

    void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
        loop_->assertInLoopThread();
        LOG_TRACE << "TcpServer::removeConnectionInLoop [" << name_
            << "] - connection " << conn->name();
        size_t n = connections_.erase(conn->name());
        assert(n == 1);
        (void)n;

        EventLoop* ioLoop = conn->getLoop();
        ioLoop->queueInLoop([conn]() { conn->connectDestroyed(); });
    }

    TcpServer::~TcpServer() {
        loop_->assertInLoopThread();
        LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

        for (auto& [name, conn] : connections_) {
            TcpConnectionPtr connPtr(conn);
            conn.reset();
            connPtr->getLoop()->runInLoop([connPtr]() {
                connPtr->connectDestroyed();
                });
        }
    }

    void TcpServer::setThreadNum(int numThreads) {
        assert(numThreads >= 0);
        threadPool_->setThreadNum(numThreads);
    }

    void TcpServer::start() {
        if (started_.fetch_add(1) == 0) {
            threadPool_->start(threadInitCallback_);
            assert(!acceptor_->listening());
            loop_->runInLoop([this]() {
                acceptor_->listen();
                });
        }
    }
}