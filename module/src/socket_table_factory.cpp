// Platform selection for Collector A and the pid source. Picks the Linux or
// macOS implementation at compile time, or a fake when the matching environment
// variable is set (tests / doctest fixtures).

#include "collector.h"
#include "process_source.h"

#include <cstdlib>
#include <memory>

namespace netgraph {

// Platform makers, defined in their own translation units (only one compiles per
// host) and the fakes.
std::unique_ptr<ISocketTable>   makeLinuxSocketTable();
std::unique_ptr<ISocketTable>   makeMacosSocketTable();
std::unique_ptr<ISocketTable>   makeFakeSocketTable(const char* path);
std::unique_ptr<IProcessSource> makeLinuxProcessSource(int64_t rootPid);
std::unique_ptr<IProcessSource> makeMacosProcessSource(int64_t rootPid);
std::unique_ptr<IProcessSource> makeFakeProcessSource(const char* spec);

std::unique_ptr<ISocketTable> makeSocketTable() {
    if (const char* fake = std::getenv("NETGRAPH_FAKE_SOCKETS"))
        return makeFakeSocketTable(fake);
#if defined(__linux__)
    return makeLinuxSocketTable();
#elif defined(__APPLE__)
    return makeMacosSocketTable();
#else
    return nullptr;  // unsupported host: no Collector A
#endif
}

std::unique_ptr<IProcessSource> makeProcessSource(int64_t rootPid) {
    if (const char* fake = std::getenv("NETGRAPH_FAKE_PIDS"))
        return makeFakeProcessSource(fake);
#if defined(__linux__)
    return makeLinuxProcessSource(rootPid);
#elif defined(__APPLE__)
    return makeMacosProcessSource(rootPid);
#else
    return nullptr;
#endif
}

}  // namespace netgraph
