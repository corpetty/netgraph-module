#pragma once

// pid <-> module-name attribution, the second half of the process-tree problem
// (see DESIGN.md "Process-tree attribution"). Collector A discovers *which* pids
// to sweep by ancestry, with zero host help; naming those pids needs the host's
// module-stats table, which a universal Basecamp module cannot read directly.
//
// This seam isolates that: an INameResolver yields the current [{name,pid}] set
// however it is obtained (path a: core_service under logoscore-cli; path b: a
// small stats-exporting core module; path c: host-fed config). netgraph_impl owns
// the SDK-backed resolver; everything here is pure so the payload decode and the
// pid<->name joins are unit-tested.
//
// FINDING (verified against logos-liblogos process_stats.cpp): the real
// logos_core_get_module_stats() payload is
//   [ { "name":..., "cpu_percent":..., "cpu_time_seconds":..., "memory_mb":... } ]
// with NO "pid" field. The pid is computed there (process->processId()) but not
// emitted, so attribution needs a one-line upstream add of "pid" to that object.
// parseModuleStats reads "pid" when present and degrades to name-only (no
// attribution, module stays null) when absent — the accepted degraded behaviour.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <logos_json.h>   // LogosMap

namespace netgraph {

struct ModuleStat {
    std::string name;
    int64_t     pid = 0;
    bool        hasPid = false;   // false => the stats feed carried no usable pid
};

// Decode a getModuleStats payload. Accepts either the bare array the host emits
// or an object wrapper {"modules":[...]}. Entries without a non-empty string
// "name" are skipped. "pid" is read when it is a positive integer.
std::vector<ModuleStat> parseModuleStats(const LogosMap& payload);

// Same, from the raw JSON string the host API returns; malformed JSON yields {}.
std::vector<ModuleStat> parseModuleStats(const std::string& json);

// pid -> name, only for entries that carried a usable pid. This is what feeds
// MergeContext.pidNames; if nothing carried a pid the map is empty and every row
// keeps module:null, which is correct.
std::unordered_map<int64_t, std::string> pidNamesFrom(const std::vector<ModuleStat>& stats);

// name -> pid, for placing a connection_source provider's rows by its pid.
// Returns 0 when the name is unknown or carried no pid.
int64_t pidForName(const std::vector<ModuleStat>& stats, const std::string& name);

// The resolver seam. stats() returns the current attribution set; an empty return
// is valid and means "no attribution available this sweep" (e.g. Basecamp today).
class INameResolver {
public:
    virtual ~INameResolver() = default;
    virtual std::vector<ModuleStat> stats() = 0;
};

// The always-empty resolver: the honest default under a host that exposes no
// reachable stats table (plain Basecamp until path b lands). Every row keeps a
// real pid and module:null.
class NullNameResolver : public INameResolver {
public:
    std::vector<ModuleStat> stats() override { return {}; }
};

// A resolver backed by a caller-supplied fetch of the raw getModuleStats
// payload. netgraph_impl builds one whose fetch binds `core_service` under the
// logoscore daemon (path a) — the SDK-touching part is a one-line lambda, while
// the decode stays here in tested pure code. A fetch that throws (no such module
// under Basecamp, a denied token, an IPC error) is swallowed to an empty result:
// "no attribution this sweep", the same honest degrade as NullNameResolver.
class CallbackNameResolver : public INameResolver {
public:
    explicit CallbackNameResolver(std::function<LogosMap()> fetch)
        : m_fetch(std::move(fetch)) {}

    std::vector<ModuleStat> stats() override {
        if (!m_fetch) return {};
        try {
            return parseModuleStats(m_fetch());
        } catch (...) {
            return {};  // provider absent / call failed => no attribution
        }
    }

private:
    std::function<LogosMap()> m_fetch;
};

}  // namespace netgraph
