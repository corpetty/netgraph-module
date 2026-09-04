#pragma once

// The internal platform-split seam for Collector A (the socket table) — the
// primary collector that produces a real graph with no cooperation from any
// module, and covers DNS and plain HTTPS.
//
// ONE interface, TWO real implementations and a fake:
//   - linux_socket_table.cpp : parse /proc/net/{tcp,tcp6,udp,udp6}, then map
//                              socket inodes to pids via /proc/<pid>/fd.
//   - macos_socket_table.cpp : libproc — proc_pidinfo(PROC_PIDLISTFDS) then
//                              proc_pidfdinfo(PROC_PIDFDSOCKETINFO) per socket fd.
//   - fake_socket_table.cpp  : a scripted table for the doctest and unit tests.
//
// HARD REQUIREMENT (handoff, platform notes): never shell out to lsof, ss, or
// netstat — a portable .lgx cannot depend on host binaries. No privileges are
// needed for our own process tree on either platform.
//
// The Linux impl + fake are written and tested (the macOS impl is written but
// unverified off-device). Pid *discovery* is a separate seam (process_source.h);
// enumerate() takes an explicit pid set so this collector stays independent of
// how the process tree is found.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace netgraph {

struct Endpoint {
    std::string addr;       // presentation form; empty when the OS reports none
    uint16_t    port = 0;
};

// One raw socket as the OS reports it, before any labelling or merge.
// `remote` is empty/zero for a listening socket and for an unconnected UDP
// socket (e.g. some DNS): those rows still appear — an unattributed/unclassified
// row is shown, never dropped.
struct SocketRow {
    int64_t     pid = 0;
    std::string transport;  // "tcp" | "udp" (quic is inferred later, udp + labels)
    std::string direction;  // "outbound" | "inbound" | "listen"
    Endpoint    local;
    Endpoint    remote;
    std::string state;      // normalized: "established" | "listen" | "closing" | ...
    uint64_t    inode = 0;  // linux socket->pid join key; 0/opaque elsewhere
};

// Enumerate every socket held by exactly these pids. The pid list is the Logos
// process tree taken from liblogos process stats (`logoscore stats`) — the tree
// is never discovered by matching process names.
//
// Runs on the sweep timer thread. Must return within a bounded read and must
// never block the module event loop (handoff constraint).
class ISocketTable {
public:
    virtual ~ISocketTable() = default;
    virtual std::vector<SocketRow> enumerate(const std::vector<int64_t>& pids) = 0;
};

// Returns the platform implementation for the host, or the fake when the
// NETGRAPH_FAKE_SOCKETS environment variable is set (tests / doctest fixture).
std::unique_ptr<ISocketTable> makeSocketTable();

}  // namespace netgraph
