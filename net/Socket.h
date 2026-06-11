#pragma once

#include "../base/noncopyable.h"

#include <cstdint>

namespace new_muduo {
	class InetAddress;
	class Socket {
	public:
		explicit Socket(int sockfd) : sockfd_(sockfd) {}

		~Socket();

		int fd() const { return sockfd_; }

		void bindAddress(const InetAddress& localaddr);
		void listen();
		int accept(InetAddress* peeraddr);

		void shutdownWrite();

		void setTcpNoDelay(bool on);
		void setReuseAddr(bool on);
		void setReusePort(bool on);
		void setKeepAlive(bool on);

		// 用于 Acceptor 在收到连接前预先设置
		static void setTcpNoDelayStatic(int fd, bool on);
		static void setReuseAddrStatic(int fd, bool on);
		static void setReusePortStatic(int fd, bool on);
		static void setKeepAliveStatic(int fd, bool on);

		int getSocketError() const;
	private:
		const int sockfd_;

	};

	
		int createNonblockingOrDie();
		int createNonblockingUdpOrDie();

		void setNonBlockAndCloseOnExec(int sockfd);

		int connect(int sockfd, const InetAddress& addr);
		bool isSelfConnect(int sockfd);

		InetAddress getLocalAddr(int sockfd);
		InetAddress getPeerAddr(int sockfd);
	
}

