#pragma once

// A DEPENDENCY INTERFACE — the contract netgraph binds to obtain per-module
// process stats (name + pid + cpu/mem) so it can attribute a swept socket to the
// module that owns it (pid -> module name). It names no concrete module by type:
// any module whose own API includes a matching `getModuleStats()` satisfies it
// (the superset rule, same as `connection_source` / openmetrics' metrics_source).
// netgraph binds it to the fixed name "core_service" at runtime via
// modules().bind_core_service("core_service").
//
// The only module that implements this today is the `core_service` RPC gateway
// that the `logoscore` CLI daemon registers (logos-logoscore-cli,
// CoreServiceImpl::getModuleStats -> logos_core_get_module_stats -> the
// process-stats library). That gateway exists ONLY under the logoscore daemon,
// not under Basecamp — so this is the design's "path (a)": good enough to make
// the M0 doctest show real module names, where the doctest runs under logoscore.
// Under any host without it, the bound call fails and netgraph degrades to
// module:null on every row (CallbackNameResolver swallows the failure). The
// production answer under Basecamp is "path (b)" — a small stats-exporting core
// module implementing this same interface — which is why the contract lives here
// rather than being hard-wired to core_service.
//
// The Logos generator parses this file and emits a BOUND wrapper class
// `CoreService` whose target module name is a runtime ctor argument, plus the
// `modules().bind_core_service(name)` factory.
//
// Types are std / LogosList because the consuming module is interface:
// "universal" — the bound wrapper inherits that api-style. `getModuleStats`
// returns a JSON array; each element is
//   { "name": <module>, "pid": <int>, "cpu_percent": <num>,
//     "cpu_time_seconds": <num>, "memory_mb": <num> }
// netgraph reads only `name` and `pid` (see name_resolver.h / parseModuleStats);
// the rest is ignored. NOTE: `pid` is emitted only once the process-stats
// `getModuleStats` one-line add lands (see DESIGN "Process-tree attribution");
// until then the array carries no pid and attribution stays off — netgraph is
// correct either way.

#include <logos_json.h>            // LogosList (nlohmann::json alias)
#include <logos_module_context.h>  // defines the `logos_events` token

class ICoreService {
public:
    // Return the host's per-module process-stats array (see the header note).
    // Labels/attribution only — this interface never affects which connections
    // exist; Collector A remains the sole authority for that.
    LogosList getModuleStats();
};
