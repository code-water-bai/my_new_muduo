// ============================================================
// coro_echo_server — 基于 C++20 协程的 CoroTcpServer 回显服务器
// 用法: ./coro_echo_server [port]
// ============================================================

#include "coro/CoroTcpServer.h"
#include "coro/CoroTcpConnection.h"
#include "coro/CoroEventLoop.h"
#include "net/EventLoop.h"
#include "net/InetAddress.h"
#include "base/Logging.h"

using namespace new_muduo;
using namespace new_muduo::coro;

int main(int argc, char* argv[]) {
    uint16_t port = 8081;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    EventLoop rawLoop;
    CoroEventLoop loop(&rawLoop);
    InetAddress addr(port);
    CoroTcpServer server(&loop, addr, "CoroEchoServer");

    // 连接建立回调（协程）：挂起保持连接，echo 由 dataCallback 处理
    server.setConnectionHandler(
        [](CoroTcpConnectionPtr conn) -> Task<void> {
            LOG_INFO << "CoroEchoServer - " << conn->peerAddress().toIpPort()
                     << " connected (fd=" << conn->fd() << ")";
            co_await std::suspend_always{};  // 永远挂起，保持连接存活
        });

    // 数据回调：收到数据立即回显
    server.setDataCallback(
        [](CoroTcpConnectionPtr conn, Buffer* buf) {
            conn->send(buf->peek(), buf->readableBytes());
            buf->retrieveAll();
        });

    server.start();

    LOG_INFO << "CoroEchoServer listening on port " << port;
    loop.loop();

    return 0;
}