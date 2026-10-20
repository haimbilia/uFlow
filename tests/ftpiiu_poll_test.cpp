#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static unsigned arrays = 0;
static bool rejectArrays = false;
void *operator new[](std::size_t size) {
    ++arrays;
    if (rejectArrays) throw std::bad_alloc();
    if (auto *p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void error(const char *, ...) {}
class Socket {
public:
    int m_fd;
    struct PollInfo { std::reference_wrapper<Socket> socket; short events, revents; };
    static int poll(PollInfo *, std::size_t, std::chrono::milliseconds);
};
// PRODUCTION_POLL

int main() {
    using namespace std::chrono_literals;
    int pair[2]; assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    Socket reader{pair[0]};
    Socket::PollInfo idle{reader, POLLIN, 123};
    rejectArrays = true;
    assert(Socket::poll(nullptr, 0, 0ms) == 0);
    for (int i = 0; i < 10000; ++i) {
        idle.revents = 123;
        assert(Socket::poll(&idle, 1, 0ms) == 0 && idle.revents == 0);
    }
    assert(arrays == 0);
    assert(::write(pair[1], "x", 1) == 1);
    assert(Socket::poll(&idle, 1, 0ms) == 1 && (idle.revents & POLLIN));
    std::vector<Socket::PollInfo> many(17, {reader, POLLIN, 0});
    assert(Socket::poll(many.data(), 16, 0ms) == 16 && arrays == 0);
    for (unsigned i = 0; i < 16; ++i) assert(many[i].revents & POLLIN);
    rejectArrays = false;
    assert(Socket::poll(many.data(), 17, 0ms) == 17 && arrays == 1);
    for (const auto &info : many) assert(info.revents & POLLIN);
    char value; assert(::read(pair[0], &value, 1) == 1);
    rejectArrays = true;
    idle.events = POLLOUT;
    assert(Socket::poll(&idle, 1, 0ms) == 1 && (idle.revents & POLLOUT));
    ::close(pair[1]); idle.events = POLLIN;
    assert(Socket::poll(&idle, 1, 0ms) == 1 && (idle.revents & POLLHUP));
    ::close(pair[0]);
    assert(Socket::poll(&idle, 1, 0ms) == 1 && (idle.revents & POLLNVAL));
    std::puts("PASS: actual FTP poll: 10,000 idle calls without allocation, read/write/closed sockets, 16/17 descriptor boundary and dynamic fallback");
}
