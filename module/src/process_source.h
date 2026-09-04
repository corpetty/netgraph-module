#pragma once

// The pid source: which processes make up the Logos process tree to sweep.
//
// FINDING (2026-09-04, from reading the SDK/core): the `logoscore stats`
// name/pid/cpu/mem table is a HOST-only API (logos_core_get_module_stats behind
// logos::host::LogosCore). It is NOT reachable from a universal Basecamp module
// — Basecamp keeps it in CoreModuleManager for QML and never re-exports it to
// modules. So the handoff's "the pid list comes from liblogos process stats"
// does not hold module-side. See DESIGN.md "Process-tree attribution".
//
// This seam therefore splits the problem:
//   - EXISTENCE (which pids): discovered module-side by process ANCESTRY — the
//     descendants of the Logos host process — never by matching process names
//     (the handoff's one explicit prohibition). Fully module-side, no host help.
//   - NAME attribution (pid -> module name): needs the host stats. Left to a
//     NameResolver supplied out of band (host-fed, a stats-wrapping core-module
//     dependency, or core_service under the logoscore-cli daemon). Absent it,
//     rows carry a real pid and module:null — still shown, per the "unlabelled
//     rows are the point" rule.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace netgraph {

struct ProcInfo {
    int64_t     pid = 0;
    std::string name;   // module name if a resolver filled it; "" otherwise
    bool        host = false;  // the Logos host process itself (tag, don't hide)
};

class IProcessSource {
public:
    virtual ~IProcessSource() = default;
    // The process tree to sweep. includeHost adds the host process (tagged host).
    virtual std::vector<ProcInfo> processes(bool includeHost) = 0;
};

// Platform ancestry-based source (Linux /proc ppid walk; macOS libproc ppid).
// rootPid seeds the tree; 0 means "derive the host from this process's own
// ancestry" (walk parents to the top non-init ancestor). Names are left empty.
std::unique_ptr<IProcessSource> makeProcessSource(int64_t rootPid = 0);

}  // namespace netgraph
