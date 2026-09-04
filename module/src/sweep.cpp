#include "sweep.h"

namespace netgraph {

std::string buildSnapshot(IProcessSource& procSource,
                          ISocketTable& sockets,
                          const std::vector<ProviderLabel>& labels,
                          bool includeHost,
                          bool enabled,
                          int64_t sweptAtMs,
                          const std::unordered_map<int64_t, std::string>& extraNames) {
    const std::vector<ProcInfo> procs = procSource.processes(includeHost);

    std::vector<int64_t> pids;
    pids.reserve(procs.size());
    MergeContext ctx;
    for (const auto& p : procs) {
        pids.push_back(p.pid);
        if (!p.name.empty()) ctx.pidNames[p.pid] = p.name;
        if (p.host) ctx.hostPids.push_back(p.pid);
    }
    // The core_service resolver's names win over the process source's (usually
    // empty for the ancestry source).
    for (const auto& kv : extraNames)
        if (!kv.second.empty()) ctx.pidNames[kv.first] = kv.second;

    const std::vector<SocketRow> rows = sockets.enumerate(pids);
    LogosMap conns = mergeConnections(rows, labels, ctx);

    LogosMap doc;
    doc["enabled"] = enabled;
    doc["swept_at"] = sweptAtMs;
    doc["connections"] = std::move(conns);
    return doc.dump();
}

}  // namespace netgraph
