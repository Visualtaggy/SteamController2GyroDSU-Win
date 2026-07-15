#pragma once
// Cross-platform socket + misc shims (Windows / POSIX)

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>

  namespace sc2 {
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
    inline void closeSocket(socket_t s) { closesocket(s); }
    inline bool socketValid(socket_t s) { return s != INVALID_SOCKET; }
    inline void setRecvTimeoutMs(socket_t s, int ms) {
        DWORD tv = (DWORD)ms;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    }
    struct SocketInit {
        SocketInit()  { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
        ~SocketInit() { WSACleanup(); }
    };
  }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>

  namespace sc2 {
    using socket_t = int;
    constexpr socket_t INVALID_SOCK = -1;
    inline void closeSocket(socket_t s) { close(s); }
    inline bool socketValid(socket_t s) { return s >= 0; }
    inline void setRecvTimeoutMs(socket_t s, int ms) {
        struct timeval tv{ ms / 1000, (ms % 1000) * 1000 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    struct SocketInit {}; // no-op on POSIX
  }
#endif

#include <string>
#include <cwchar>

namespace sc2 {
// hidapi reports serial numbers as wchar_t*. Convert to a narrow string
// portably (the original code reinterpret_cast'ed the bytes, which breaks
// on Windows where wchar_t is UTF-16).
inline std::string narrow(const wchar_t* ws) {
    if (!ws) return {};
    std::string out;
    out.reserve(wcslen(ws));
    for (const wchar_t* p = ws; *p; ++p)
        out.push_back(*p < 128 ? (char)*p : '?');
    return out;
}
} // namespace sc2
