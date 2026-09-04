// Collector A on macOS via libproc — no /proc here. For each target pid,
// proc_pidinfo(PROC_PIDLISTFDS) lists the fds, and proc_pidfdinfo(
// PROC_PIDFDSOCKETINFO) returns each socket's addresses/state. pid comes
// straight from the call, so no inode->pid join is needed (inode stays 0).
// No shelling out; no privileges needed for our own process tree.
//
// UNVERIFIED ON THIS HOST: written from the documented libproc/sys_proc_info
// API but compiled/run only on macOS (the dev sandbox is Linux). Verify on
// Apple Silicon before relying on it — the address/port field access in
// in_sockinfo and the TCP state constants are the likely places to check.
//
// Compiled only on Apple platforms (guarded by socket_table_factory.cpp / CMake).

#if defined(__APPLE__)

#include "collector.h"
#include "proc_net_parse.h"  // inferDirections()

#include <libproc.h>
#include <sys/proc_info.h>
#include <netinet/in.h>
#include <netinet/tcp_fsm.h>
#include <arpa/inet.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace netgraph {

namespace {

std::string fmtV4(const struct in_addr& a) {
    char buf[INET_ADDRSTRLEN];
    return inet_ntop(AF_INET, &a, buf, sizeof(buf)) ? std::string(buf) : std::string();
}
std::string fmtV6(const struct in6_addr& a) {
    char buf[INET6_ADDRSTRLEN];
    return inet_ntop(AF_INET6, &a, buf, sizeof(buf)) ? std::string(buf) : std::string();
}

std::string tcpState(int s) {
    switch (s) {
        case TCPS_LISTEN:       return "listen";
        case TCPS_ESTABLISHED:  return "established";
        case TCPS_SYN_SENT:     return "syn_sent";
        case TCPS_SYN_RECEIVED: return "syn_recv";
        case TCPS_CLOSED:       return "closed";
        case TCPS_CLOSE_WAIT:
        case TCPS_FIN_WAIT_1:
        case TCPS_CLOSING:
        case TCPS_LAST_ACK:
        case TCPS_FIN_WAIT_2:
        case TCPS_TIME_WAIT:    return "closing";
        default:                return "unknown";
    }
}

// Pull local/remote endpoints out of an in_sockinfo (shared by TCP and UDP).
void readEndpoints(const struct in_sockinfo& in, Endpoint& local, Endpoint& remote) {
    local.port  = ntohs(static_cast<uint16_t>(in.insi_lport));
    remote.port = ntohs(static_cast<uint16_t>(in.insi_fport));
    if (in.insi_vflag & INI_IPV6) {
        local.addr  = fmtV6(in.insi_laddr.ina_6);
        remote.addr = fmtV6(in.insi_faddr.ina_6);
    } else {
        local.addr  = fmtV4(in.insi_laddr.ina_46.i46a_addr4);
        remote.addr = fmtV4(in.insi_faddr.ina_46.i46a_addr4);
    }
    // A zero foreign address/port means no peer.
    if (remote.port == 0 &&
        (remote.addr.empty() || remote.addr == "0.0.0.0" || remote.addr == "::")) {
        remote = Endpoint{};
    }
}

void collectPidSockets(int64_t pid, std::vector<SocketRow>& out) {
    int size = proc_pidinfo(static_cast<int>(pid), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (size <= 0) return;
    std::vector<struct proc_fdinfo> fds(size / sizeof(struct proc_fdinfo));
    size = proc_pidinfo(static_cast<int>(pid), PROC_PIDLISTFDS, 0, fds.data(), size);
    if (size <= 0) return;
    const int n = size / static_cast<int>(sizeof(struct proc_fdinfo));

    for (int i = 0; i < n; ++i) {
        if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET) continue;
        struct socket_fdinfo si;
        std::memset(&si, 0, sizeof(si));
        int r = proc_pidfdinfo(static_cast<int>(pid), fds[i].proc_fd,
                               PROC_PIDFDSOCKETINFO, &si, PROC_PIDFDSOCKETINFO_SIZE);
        if (r < static_cast<int>(PROC_PIDFDSOCKETINFO_SIZE)) continue;

        const int family = si.psi.soi_family;
        if (family != AF_INET && family != AF_INET6) continue;  // skip unix/other

        SocketRow row;
        row.pid = pid;
        if (si.psi.soi_kind == SOCKINFO_TCP) {
            row.transport = "tcp";
            const auto& t = si.psi.soi_proto.pri_tcp;
            readEndpoints(t.tcpsi_ini, row.local, row.remote);
            row.state = tcpState(t.tcpsi_state);
            if (row.remote.addr.empty() && row.state != "listen") continue;  // not useful
        } else if (si.psi.soi_kind == SOCKINFO_IN) {
            row.transport = "udp";
            readEndpoints(si.psi.soi_proto.pri_in, row.local, row.remote);
            row.state = "established";
        } else {
            continue;
        }
        out.push_back(std::move(row));
    }
}

class MacosSocketTable : public ISocketTable {
public:
    std::vector<SocketRow> enumerate(const std::vector<int64_t>& pids) override {
        std::vector<SocketRow> rows;
        for (int64_t pid : pids) collectPidSockets(pid, rows);
        inferDirections(rows);
        return rows;
    }
};

}  // namespace

std::unique_ptr<ISocketTable> makeMacosSocketTable() {
    return std::unique_ptr<ISocketTable>(new MacosSocketTable());
}

}  // namespace netgraph

#endif  // __APPLE__
