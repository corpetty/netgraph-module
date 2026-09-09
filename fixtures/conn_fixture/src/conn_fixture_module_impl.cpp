#include "conn_fixture_module_impl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>

// A doc-test fixture. POSIX sockets only, so the one body compiles on Linux and
// macOS alike. No SDK use beyond the LogosModuleContext base.

ConnFixtureModuleImpl::~ConnFixtureModuleImpl() {
    closeAll();
}

void ConnFixtureModuleImpl::closeAll() {
    for (int fd : m_fds)
        if (fd >= 0) ::close(fd);
    m_fds.clear();
}

int64_t ConnFixtureModuleImpl::openLoopback(int64_t port) {
    if (port <= 0 || port > 65535) return -1;

    std::lock_guard<std::mutex> lock(m_mutex);
    closeAll();  // idempotent: drop any prior connection first

    const uint16_t p = static_cast<uint16_t>(port);

    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return -2;
    int one = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(p);

    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { ::close(lfd); return -3; }
    if (::listen(lfd, 1) != 0) { ::close(lfd); return -4; }

    int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) { ::close(lfd); return -5; }
    if (::connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(cfd); ::close(lfd); return -6;
    }

    int afd = ::accept(lfd, nullptr, nullptr);
    if (afd < 0) { ::close(cfd); ::close(lfd); return -7; }

    // The connection is ESTABLISHED; hold listener + client + accepted open so it
    // stays visible in this process's /proc fds for the module's lifetime.
    m_fds = {lfd, cfd, afd};
    return static_cast<int64_t>(::getpid());
}
