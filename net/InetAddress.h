#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <string>


namespace new_muduo {
	class InetAddress {
	public:
		explicit InetAddress(uint16_t port, std::string ip = "127.0.0.1") {
			bzero(&addr_, sizeof(addr_));
			addr_.sin_family = AF_INET;
			addr_.sin_addr.s_addr = inet_addr(ip.c_str());
			addr_.sin_port = htons(port);
		}

		explicit InetAddress(const ::sockaddr_in& addr) {
			addr_ = addr;
		}
		InetAddress(){}

		std::string toIp()const {
			char buf[64] = { 0 };
			inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
			return buf;
		}

		std::string toIpPort() const {
			char buf[64] = { 0 };
			inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
			size_t len = strlen(buf);
			uint16_t port = ntohs(addr_.sin_port);
			sprintf(buf + len, ":%u", port);
			return buf;
		}
		uint16_t toPort() const {
			return ntohs(addr_.sin_port);
		}

		const ::sockaddr_in* getSockAddr()const { return &addr_; }
		void setAddr(const ::sockaddr_in& addr) { addr_ = addr; }

	private:
		::sockaddr_in addr_;
	};
}