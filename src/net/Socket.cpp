#include "Socket.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define CLOSESOCK closesocket
#define SHUT_BOTH SD_BOTH
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#define CLOSESOCK ::close
#define SHUT_BOTH SHUT_RDWR
#endif

namespace mob {
bool Socket::globalInit() {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
#else
    return true;
#endif
}
Socket::~Socket() { close(); }

bool Socket::connectLocal(uint16_t port, int timeoutMs) {
    close();
    fd_ = (sock_t)::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == kInvalid) return false;
    int one = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(fd_, FIONBIO, &nb);
    ::connect(fd_, (sockaddr*)&a, sizeof a);
    fd_set ws, es;
    FD_ZERO(&ws); FD_ZERO(&es);
    FD_SET(fd_, &ws); FD_SET(fd_, &es);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int r = select(0, nullptr, &ws, &es, &tv);
    nb = 0;
    ioctlsocket(fd_, FIONBIO, &nb);
    if (r <= 0 || FD_ISSET(fd_, &es)) { close(); return false; }
#else
    int fl = fcntl(fd_, F_GETFL);
    fcntl(fd_, F_SETFL, fl | O_NONBLOCK);
    int r = ::connect(fd_, (sockaddr*)&a, sizeof a);
    if (r != 0 && errno == EINPROGRESS) {
        pollfd p{fd_, POLLOUT, 0};
        if (poll(&p, 1, timeoutMs) <= 0) { close(); return false; }
        int err = 0; socklen_t l = sizeof err;
        getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &l);
        if (err) { close(); return false; }
    } else if (r != 0) { close(); return false; }
    fcntl(fd_, F_SETFL, fl);
#endif
    return true;
}

void Socket::setRecvBuffer(int bytes) {
    if (valid()) setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (const char*)&bytes, sizeof bytes);
}

bool Socket::recvAll(void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    while (len) {
        long n = recvSome(p, len);
        if (n <= 0) return false;
        p += n; len -= (size_t)n;
    }
    return true;
}
long Socket::recvSome(void* buf, size_t len) {
    if (!valid()) return -1;
    return (long)::recv(fd_, (char*)buf, (int)len, 0);
}
bool Socket::sendAll(const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len) {
        if (!valid()) return false;
        long n = (long)::send(fd_, p, (int)len, 0);
        if (n <= 0) return false;
        p += n; len -= (size_t)n;
    }
    return true;
}
void Socket::shutdown() { if (valid()) ::shutdown(fd_, SHUT_BOTH); }
void Socket::close() {
    if (valid()) { CLOSESOCK(fd_); fd_ = kInvalid; }
}
}
