// ============================================================
// echo_server — 基于传统 TcpServer 的回显服务器
// 用法: ./echo_server [port]
// ============================================================

#include "net/TcpServer.h"
#include "net/EventLoop.h"
#include "net/InetAddress.h"
#include "net/TcpConnection.h"
#include "net/Buffer.h"
#include "base/Logging.h"

using namespace new_muduo;

class EchoServer {
public:
    EchoServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
        : server_(loop, addr, name)
    {
        server_.setConnectionCallback(
            [this](const TcpConnectionPtr& conn) { onConnection(conn); });
        server_.setMessageCallback(
            [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp ts) {
                onMessage(conn, buf, ts);
            });
    }

    void setThreadNum(int n) { server_.setThreadNum(n); }
    void start() { server_.start(); }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort()
                     << " -> " << conn->localAddress().toIpPort() << " : online";
        } else {
            LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort()
                     << " -> " << conn->localAddress().toIpPort() << " : offline";
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp /*ts*/) {
        std::string msg = buf->retrieveAllAsString();
        LOG_INFO << conn->name() << " echo " << msg.size() << " bytes";
        conn->send(msg);

        if (msg == "quit\r\n" || msg == "quit\n") {
            conn->shutdown();
        }
    }

    TcpServer server_;
};

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    EventLoop loop;
    InetAddress addr(port);
    EchoServer server(&loop, addr, "EchoServer");

    server.setThreadNum(4);  // 4 个 IO 线程
    server.start();

    LOG_INFO << "EchoServer listening on port " << port;
    loop.loop();

    return 0;
}
