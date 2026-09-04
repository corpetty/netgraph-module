// Collector A on Linux: parse /proc/net/{tcp,tcp6,udp,udp6} for the system's
// sockets, then keep only those owned by the target pids by matching socket
// inodes found under /proc/<pid>/fd. No privileges needed for our own process
// tree; no shelling out to lsof/ss/netstat.
//
// Compiled only on Linux; self-guarded so the file can be listed unconditionally.

#if defined(__linux__)

#include "collector.h"
#include "proc_net_parse.h"

#include <dirent.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace netgraph {

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Every socket inode the given pid holds open, read from /proc/<pid>/fd/* as
// "socket:[<inode>]" symlink targets. A pid we cannot read (gone, or not ours)
// contributes nothing rather than failing the sweep.
void collectPidInodes(int64_t pid, std::unordered_map<uint64_t, int64_t>& inodeToPid) {
    const std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
    DIR* d = opendir(fdDir.c_str());
    if (!d) return;
    struct dirent* ent;
    char linkbuf[256];
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        const std::string link = fdDir + "/" + ent->d_name;
        ssize_t n = readlink(link.c_str(), linkbuf, sizeof(linkbuf) - 1);
        if (n <= 0) continue;
        linkbuf[n] = '\0';
        // "socket:[12345]"
        if (std::strncmp(linkbuf, "socket:[", 8) != 0) continue;
        uint64_t inode = 0;
        for (const char* p = linkbuf + 8; *p && *p != ']'; ++p) {
            if (*p < '0' || *p > '9') { inode = 0; break; }
            inode = inode * 10 + uint64_t(*p - '0');
        }
        if (inode != 0) inodeToPid[inode] = pid;  // last writer wins; shared inodes are rare
    }
    closedir(d);
}

class LinuxSocketTable : public ISocketTable {
public:
    std::vector<SocketRow> enumerate(const std::vector<int64_t>& pids) override {
        // 1. Parse the whole system socket table once (keyed by inode).
        std::vector<SocketRow> all;
        const struct { const char* path; ProcNetKind kind; } files[] = {
            {"/proc/net/tcp",  ProcNetKind::Tcp4},
            {"/proc/net/tcp6", ProcNetKind::Tcp6},
            {"/proc/net/udp",  ProcNetKind::Udp4},
            {"/proc/net/udp6", ProcNetKind::Udp6},
        };
        for (const auto& f : files) {
            auto rows = parseProcNet(f.kind, slurp(f.path));
            for (auto& r : rows) all.push_back(std::move(r));
        }

        // 2. Map inode -> pid for exactly the target pids.
        std::unordered_map<uint64_t, int64_t> inodeToPid;
        for (int64_t pid : pids) collectPidInodes(pid, inodeToPid);

        // 3. Keep only rows owned by a target pid; stamp the pid.
        std::vector<SocketRow> owned;
        owned.reserve(all.size());
        for (auto& r : all) {
            auto it = inodeToPid.find(r.inode);
            if (it == inodeToPid.end()) continue;
            r.pid = it->second;
            owned.push_back(std::move(r));
        }

        // 4. Assign inbound/outbound/listen using this set's listen ports.
        inferDirections(owned);
        return owned;
    }
};

}  // namespace

std::unique_ptr<ISocketTable> makeLinuxSocketTable() {
    return std::unique_ptr<ISocketTable>(new LinuxSocketTable());
}

}  // namespace netgraph

#endif  // __linux__
