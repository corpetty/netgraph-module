// Pid discovery on Linux by process ancestry: collect the descendants of the
// Logos host process from /proc/<pid>/stat parent links. Name-independent (the
// handoff forbids discovery by process name); needs no privileges for our own
// tree. Compiled only on Linux.

#if defined(__linux__)

#include "process_source.h"

#include <unistd.h>

#include <dirent.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace netgraph {

namespace {

// Read ppid from /proc/<pid>/stat. The comm field (field 2) is parenthesized and
// may contain spaces/parens, so scan from the LAST ')' — ppid is the 2nd token
// after it (state, then ppid). Returns -1 on failure.
int64_t readPpid(int64_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    if (!f) return -1;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto rp = s.rfind(')');
    if (rp == std::string::npos) return -1;
    std::istringstream rest(s.substr(rp + 1));
    std::string state;
    int64_t ppid = -1;
    rest >> state >> ppid;  // state, then ppid
    return ppid;
}

std::vector<int64_t> allPids() {
    std::vector<int64_t> pids;
    DIR* d = opendir("/proc");
    if (!d) return pids;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        char* end = nullptr;
        long v = std::strtol(e->d_name, &end, 10);
        if (end && *end == '\0' && v > 0) pids.push_back(v);
    }
    closedir(d);
    return pids;
}

// Walk parents from `start` up to (but not including) pid 1, returning the
// highest ancestor whose own parent is 1 — the top of this app's tree.
int64_t deriveRoot(int64_t start) {
    int64_t cur = start, top = start;
    for (int hops = 0; hops < 1024 && cur > 1; ++hops) {
        int64_t pp = readPpid(cur);
        if (pp <= 1) { top = cur; break; }
        top = pp;
        cur = pp;
    }
    return top;
}

class LinuxProcessSource : public IProcessSource {
public:
    explicit LinuxProcessSource(int64_t rootPid) : m_root(rootPid) {}

    std::vector<ProcInfo> processes(bool includeHost) override {
        const int64_t root = m_root > 0 ? m_root : deriveRoot(getpid());

        // Build child adjacency once, then BFS from root.
        const auto pids = allPids();
        std::unordered_map<int64_t, std::vector<int64_t>> children;
        for (int64_t p : pids) {
            int64_t pp = readPpid(p);
            if (pp > 0) children[pp].push_back(p);
        }

        std::vector<ProcInfo> out;
        std::unordered_set<int64_t> seen;
        std::vector<int64_t> stack = {root};
        while (!stack.empty()) {
            int64_t p = stack.back();
            stack.pop_back();
            if (!seen.insert(p).second) continue;
            const bool isHost = (p == root);
            if (!isHost || includeHost) {
                out.push_back(ProcInfo{p, std::string(), isHost});
            }
            auto it = children.find(p);
            if (it != children.end())
                for (int64_t c : it->second) stack.push_back(c);
        }
        return out;
    }

private:
    int64_t m_root;
};

}  // namespace

std::unique_ptr<IProcessSource> makeLinuxProcessSource(int64_t rootPid) {
    return std::unique_ptr<IProcessSource>(new LinuxProcessSource(rootPid));
}

}  // namespace netgraph

#endif  // __linux__
