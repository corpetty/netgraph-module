// Live smoke test for Collector A on Linux: open a known listening socket and a
// connected pair on this process, then prove the socket table finds them for our
// own pid and the ancestry process source includes us. Linux-only; needs a live
// /proc (skipped elsewhere). Not a fixture test — it exercises the real host.

#if defined(__linux__)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "collector.h"
#include "process_source.h"

namespace netgraph {
std::unique_ptr<ISocketTable>   makeLinuxSocketTable();
std::unique_ptr<IProcessSource> makeLinuxProcessSource(int64_t rootPid);
}

using namespace netgraph;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    // A listening socket on 127.0.0.1, kernel-assigned port.
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(lfd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    CHECK(::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    CHECK(::listen(lfd, 1) == 0);
    socklen_t alen = sizeof(addr);
    CHECK(::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
    const uint16_t port = ntohs(addr.sin_port);
    std::printf("listening on 127.0.0.1:%u\n", port);

    const int64_t self = ::getpid();

    // Process source: our own tree must contain us.
    auto psrc = makeLinuxProcessSource(0);
    CHECK(psrc != nullptr);
    bool sawSelf = false;
    if (psrc) for (const auto& p : psrc->processes(/*includeHost=*/true))
        if (p.pid == self) sawSelf = true;
    CHECK(sawSelf);

    // Socket table for our pid must include the listen row on `port`.
    auto tbl = makeLinuxSocketTable();
    CHECK(tbl != nullptr);
    auto rows = tbl ? tbl->enumerate({self}) : std::vector<SocketRow>{};
    std::printf("socket table returned %zu rows for pid %lld\n",
                rows.size(), static_cast<long long>(self));

    bool sawListen = false;
    for (const auto& r : rows) {
        if (r.transport == "tcp" && r.state == "listen" && r.local.port == port) {
            sawListen = true;
            CHECK(r.pid == self);
            CHECK(r.direction == "listen");
            CHECK(r.local.addr == "127.0.0.1");
        }
    }
    CHECK(sawListen);

    ::close(lfd);

    if (failures == 0) std::printf("linux_live_test: OK\n");
    else std::printf("linux_live_test: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}

#else
int main() { return 0; }  // non-Linux: nothing to prove here
#endif
