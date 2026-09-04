// Pid discovery on macOS by ancestry via libproc: list all pids, read each
// pid's parent (PROC_PIDT_SHORTBSDINFO -> pbsi_ppid), and walk the tree from the
// Logos host root. Name-independent (the handoff forbids discovery by process
// name); no privileges needed for our own tree.
//
// UNVERIFIED ON THIS HOST: compiled/run only on macOS. Verify the pbsi_ppid
// access and proc_listpids sizing on Apple Silicon.
//
// Compiled only on Apple platforms (guarded by socket_table_factory.cpp / CMake).

#if defined(__APPLE__)

#include "process_source.h"

#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace netgraph {

namespace {

int64_t readPpid(int64_t pid) {
    struct proc_bsdshortinfo bi;
    int r = proc_pidinfo(static_cast<int>(pid), PROC_PIDT_SHORTBSDINFO, 0,
                         &bi, PROC_PIDT_SHORTBSDINFO_SIZE);
    if (r < static_cast<int>(PROC_PIDT_SHORTBSDINFO_SIZE)) return -1;
    return static_cast<int64_t>(bi.pbsi_ppid);
}

std::vector<int64_t> allPids() {
    std::vector<int64_t> out;
    int cap = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (cap <= 0) return out;
    std::vector<pid_t> buf(cap / sizeof(pid_t) + 16);
    int got = proc_listpids(PROC_ALL_PIDS, 0, buf.data(),
                            static_cast<int>(buf.size() * sizeof(pid_t)));
    if (got <= 0) return out;
    const int n = got / static_cast<int>(sizeof(pid_t));
    for (int i = 0; i < n; ++i) if (buf[i] > 0) out.push_back(buf[i]);
    return out;
}

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

class MacosProcessSource : public IProcessSource {
public:
    explicit MacosProcessSource(int64_t rootPid) : m_root(rootPid) {}

    std::vector<ProcInfo> processes(bool includeHost) override {
        const int64_t root = m_root > 0 ? m_root : deriveRoot(getpid());

        std::unordered_map<int64_t, std::vector<int64_t>> children;
        for (int64_t p : allPids()) {
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
            if (!isHost || includeHost) out.push_back(ProcInfo{p, std::string(), isHost});
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

std::unique_ptr<IProcessSource> makeMacosProcessSource(int64_t rootPid) {
    return std::unique_ptr<IProcessSource>(new MacosProcessSource(rootPid));
}

}  // namespace netgraph

#endif  // __APPLE__
