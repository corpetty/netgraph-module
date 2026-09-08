#include "netgraph_impl.h"

#include <chrono>
#include <cstdint>

#include <logos_json.h>  // LogosMap

#include "sweep.h"
#include "merge.h"
#include "provider_parse.h"   // netgraph::parseConnectionSourcePayload (pure)
#include "name_resolver.h"    // netgraph::INameResolver, ModuleStat (pure)

// Generated at build time by logos-cpp-generator. Defines `LogosModules` with
// the `bind_connection_source(moduleName)` factory (because metadata.json
// declares an interface_dependency on `connection_source`). Included only in the
// .cpp so the impl header the generator parses stays free of codegen types.
//
// Collector B needs the generated bind wrapper; Collector A needs none of it.
#include "logos_sdk.h"

namespace {

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

NetgraphImpl::~NetgraphImpl() {
    stopSweep();
}

// Build the pid<->name resolver for the chosen attribution path.
//
// Path (a) — wired here: bind the logoscore daemon's `core_service` gateway
// (declared as an interface_dependency in metadata.json → generated
// `bind_core_service`) and decode its getModuleStats() through the pure
// netgraph::parseModuleStats. The bound call is a one-line lambda behind
// CallbackNameResolver; if `core_service` is absent (any host that is not the
// logoscore daemon — e.g. Basecamp), or the call is denied/errors, the resolver
// swallows it and returns nothing, so every row keeps a real pid and
// module:null. That honest degrade is exactly what NullNameResolver did, so
// path (a) is a strict superset and safe to install unconditionally.
//
// Path (b), production under Basecamp: a small stats-exporting core module
// implementing the SAME ICoreService interface, bound to its own name — no code
// change here beyond the bound name, so it drops in when it exists.
//
// BOTH paths need a pid in the stats payload. getModuleStats() (now the
// process-stats library) computes the pid but historically did not emit it;
// parseModuleStats reads "pid" when present and degrades to name-only
// (module:null) when absent, so netgraph is correct before and after the
// process-stats one-line "pid" add. See DESIGN "Process-tree attribution".
std::unique_ptr<netgraph::INameResolver> NetgraphImpl::makeResolver() {
    return std::make_unique<netgraph::CallbackNameResolver>(
        [this]() -> LogosMap {
            return modules().bind_core_service("core_service").getModuleStats();
        });
}

int64_t NetgraphImpl::setEnabled(const std::string& configJson) {
    bool startNow = false, stopNow = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        LogosMap cfg;
        try {
            cfg = LogosMap::parse(configJson);
        } catch (...) {
            return 0;
        }

        const bool enable = cfg.value("enabled", false);

        int sweepMs = cfg.value("sweep_ms", m_config.sweepMs);
        if (sweepMs < 250) sweepMs = 250;  // never hammer the socket table
        m_config.sweepMs = sweepMs;
        m_config.includeHost = cfg.value("include_host", m_config.includeHost);
        m_config.rootPid = cfg.value("root_pid", m_config.rootPid);

        m_config.sources.clear();
        if (cfg.contains("sources") && cfg["sources"].is_array()) {
            for (const auto& s : cfg["sources"])
                if (s.is_string() && !s.get<std::string>().empty())
                    m_config.sources.push_back(s.get<std::string>());
        }

        if (enable == m_enabled) return 0;  // config updated, no state change
        m_enabled = enable;

        if (enable) {
            startNow = true;
        } else {
            stopNow = true;
            m_snapshot = R"({"enabled":false,"swept_at":0,"connections":[]})";
            m_lastSockets = m_lastAttributed = m_lastDerived = 0;
        }
    }
    // Thread lifecycle happens outside the data lock (join must not hold it).
    if (startNow) startSweep();
    if (stopNow) stopSweep();
    return 1;
}

std::string NetgraphImpl::snapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;  // last completed sweep; never blocks on a live one
}

std::string NetgraphImpl::getInfo() {
    std::lock_guard<std::mutex> lock(m_mutex);
    LogosMap info;
    info["enabled"] = m_enabled;
    info["sweeping"] = m_running;
    info["sweep_ms"] = m_config.sweepMs;
    info["include_host"] = m_config.includeHost;
    info["sockets"] = m_lastSockets;
    info["attributed"] = m_lastAttributed;
    info["derived"] = m_lastDerived;
    LogosMap sources = LogosMap::array();
    for (const auto& s : m_config.sources) sources.push_back(s);
    info["sources"] = std::move(sources);
    return info.dump();
}

void NetgraphImpl::startSweep() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) return;
    if (!m_sockets)    m_sockets = netgraph::makeSocketTable();
    if (!m_procSource) m_procSource = netgraph::makeProcessSource(m_config.rootPid);
    if (!m_resolver)   m_resolver = makeResolver();
    m_stop = false;
    m_running = true;
    m_thread = std::thread([this] { runLoop(); });
}

void NetgraphImpl::stopSweep() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;
}

void NetgraphImpl::runLoop() {
    for (;;) {
        Config cfg;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stop) break;
            cfg = m_config;
        }
        doSweepAndPublish(cfg);

        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(cfg.sweepMs), [this] { return m_stop; });
        if (m_stop) break;
    }
}

void NetgraphImpl::doSweepAndPublish(const Config& cfg) {
    if (!m_sockets || !m_procSource) return;  // unsupported host

    // One attribution read per sweep, shared by Collector A (pid->name on every
    // socket row) and Collector B (name->pid to place a provider's rows).
    std::vector<netgraph::ModuleStat> stats;
    if (m_resolver) {
        try { stats = m_resolver->stats(); } catch (...) {}  // never break a sweep
    }
    std::unordered_map<int64_t, std::string> extraNames = netgraph::pidNamesFrom(stats);

    std::vector<netgraph::ProviderLabel> labels = collectProviderLabels(cfg, stats);
    std::string doc = netgraph::buildSnapshot(*m_procSource, *m_sockets, labels,
                                              cfg.includeHost, /*enabled=*/true, nowMs(),
                                              extraNames);

    // Recount for getInfo without re-walking: parse is cheap relative to a sweep.
    int sockets = 0, attributed = 0, derived = 0;
    try {
        LogosMap d = LogosMap::parse(doc);
        for (const auto& r : d["connections"]) {
            if (r.value("derived", false)) ++derived; else ++sockets;
            if (!r["module"].is_null()) ++attributed;
        }
    } catch (...) {}

    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot = std::move(doc);
    m_lastSockets = sockets;
    m_lastAttributed = attributed;
    m_lastDerived = derived;
}

std::vector<netgraph::ProviderLabel> NetgraphImpl::collectProviderLabels(
        const Config& cfg, const std::vector<netgraph::ModuleStat>& stats) {
    std::vector<netgraph::ProviderLabel> labels;

    for (const auto& name : cfg.sources) {
        // The provider's own pid, so its rows key against its sockets. 0 when the
        // resolver can't attribute it yet — the label still enriches by endpoint.
        const int64_t pid = netgraph::pidForName(stats, name);

        // Fetch the payload through the generated bound wrapper. A module that
        // doesn't implement collectConnections(), or that errors/times out, is
        // skipped — one bad provider never breaks a sweep.
        LogosMap payload;
        try {
            payload = modules().bind_connection_source(name).collectConnections();
        } catch (...) {
            continue;
        }

        // Pure parse + validation (provider_parse.cpp). Malformed entries are
        // dropped there; a malformed top-level shape yields no labels.
        // TODO(M3): validate against the connection_source CDDL schema before this,
        // rejecting a non-conforming feed wholesale rather than per-entry.
        netgraph::ProviderParseResult r =
            netgraph::parseConnectionSourcePayload(name, pid, payload);
        for (auto& lbl : r.labels) labels.push_back(std::move(lbl));
    }

    return labels;
}
