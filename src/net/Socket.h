#pragma once
#include <cstdint>
#include <cstddef>

namespace mob {
// Socket TCP mínimo (loopback até o túnel ADB). TCP_NODELAY sempre ativo: sem Nagle,
// cada evento de toque sai imediatamente em vez de esperar agrupamento.
class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    static bool globalInit();
    bool connectLocal(uint16_t port, int timeoutMs);
    bool recvAll(void* buf, size_t len);   // bloqueia até len bytes (thread dedicada)
    long recvSome(void* buf, size_t len);
    bool sendAll(const void* buf, size_t len);
    void shutdown();                        // desbloqueia recv em outra thread
    void close();
    bool valid() const { return fd_ != kInvalid; }
    void setRecvBuffer(int bytes);
private:
#ifdef _WIN32
    using sock_t = uintptr_t;
    static constexpr sock_t kInvalid = ~sock_t(0);
#else
    using sock_t = int;
    static constexpr sock_t kInvalid = -1;
#endif
    sock_t fd_ = kInvalid;
};
}
