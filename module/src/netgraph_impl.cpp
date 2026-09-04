#include "netgraph_impl.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>

#include <logos_json.h>  // LogosMap

#include "attribution.h"
#include "sweep.h"
#include "merge.h"

// Generated at build time by logos-cpp-generator. Defines `LogosModules` with
// the `bind_connection_source(moduleName)` factory (because metadata.json
// declares an interface_dependency on `connection_source`). Included only in the
// .cpp so the impl header the generator parses stays free of codegen types.
//
// TODO(M3/attribution): #include "logos_sdk.h" — enable when Collector B binds
// providers. Collector A needs none of it.

namespace {

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

NetgraphImpl::~NetgraphImpl() {
    stopSweep();
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

        m_config.statsSource = cfg.value("stats_source", m_config.statsSource);
        m_config.statsToken = cfg.value("stats_token", m_config.statsToken);

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

    // pid <-> name attribution, once per sweep, shared by the base module label
    // and Collector B's row placement.
    std::unordered_map<int64_t, std::string> pidNames = resolveNames(cfg);
    std::unordered_map<std::string, int64_t> name2pid;
    for (const auto& kv : pidNames) name2pid[kv.second] = kv.first;

    std::vector<netgraph::ProviderLabel> labels = collectProviderLabels(cfg, name2pid);
    std::string doc = netgraph::buildSnapshot(*m_procSource, *m_sockets, labels,
                                              cfg.includeHost, /*enabled=*/true, nowMs(),
                                              pidNames);

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

// ── The two SDK-coupled fetches, isolated ────────────────────────────────────
// These are the only points that touch inter-module IPC, and the only code in
// this module not yet exercised on-host. Everything downstream (parseModuleStats,
// parseProviderConnections, merge) is pure and unit-tested. Each returns an empty
// document until its one SDK call is confirmed on a real host; Collector A runs
// regardless, so the module produces a graph today and these light up Collector B
// and attribution without touching the tested logic.
namespace {

// getModuleStats() on the configured stats source. Under the logoscore daemon
// that is `core_service` (token-gated, no shipped .lidl → a by-name invoke via
// the SDK LpClient — logos_lp_client.h invoke()). For Basecamp production, point
// statsSource at the stats-exporting core module declared as a netgraph
// dependency and call it through its typed wrapper (modules().<dep>...).
LogosMap fetchModuleStats(LogosModuleContext& /*ctx*/,
                          const std::string& /*statsSource*/,
                          const std::string& /*token*/) {
    // TODO(on-host): LpClient invoke of "getModuleStats" on statsSource with the
    // token; parse the returned JSON string to LogosMap. Returns empty until
    // wired — attribution then degrades to null, which is correct.
    return LogosMap::object();
}

// collectConnections() on one bound connection_source provider.
LogosMap fetchProviderConnections(LogosModuleContext& /*ctx*/,
                                  const std::string& /*moduleName*/) {
    // TODO(on-host): modules().bind_connection_source(moduleName).collectConnections()
    // Returns empty until wired — Collector B contributes nothing, Collector A
    // is unaffected.
    return LogosMap::object();
}

}  // namespace

std::unordered_map<int64_t, std::string> NetgraphImpl::resolveNames(const Config& cfg) {
    if (cfg.statsSource.empty()) return {};
    try {
        LogosMap stats = fetchModuleStats(*this, cfg.statsSource, cfg.statsToken);
        return netgraph::parseModuleStats(stats);
    } catch (...) {
        return {};  // unreachable stats source => no attribution (rows still shown)
    }
}

std::vector<netgraph::ProviderLabel> NetgraphImpl::collectProviderLabels(
    const Config& cfg, const std::unordered_map<std::string, int64_t>& name2pid) {
    std::vector<netgraph::ProviderLabel> labels;
    for (const auto& name : cfg.sources) {
        auto it = name2pid.find(name);
        if (it == name2pid.end()) continue;  // no pid resolved yet => can't place rows
        const int64_t pid = it->second;
        try {
            LogosMap payload = fetchProviderConnections(*this, name);
            // TODO(M3): validate payload against the connection_source CDDL schema
            // and reject a malformed feed rather than merging it.
            auto rows = netgraph::parseProviderConnections(name, pid, payload);
            for (auto& r : rows) labels.push_back(std::move(r));
        } catch (...) { /* one bad provider never breaks a sweep */ }
    }
    return labels;
}
