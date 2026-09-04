#include "netgraph_impl.h"

#include <chrono>
#include <cstdint>

#include <logos_json.h>  // LogosMap

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

    std::vector<netgraph::ProviderLabel> labels = collectProviderLabels(cfg);
    std::string doc = netgraph::buildSnapshot(*m_procSource, *m_sockets, labels,
                                              cfg.includeHost, /*enabled=*/true, nowMs());

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

std::vector<netgraph::ProviderLabel> NetgraphImpl::collectProviderLabels(const Config& cfg) {
    std::vector<netgraph::ProviderLabel> labels;
    (void)cfg;
    // TODO(attribution + M3): for each name in cfg.sources:
    //   LogosMap payload = modules().bind_connection_source(name).collectConnections();
    //   (empty/error => skip; one bad provider never breaks a sweep)
    //   validate payload against the connection_source CDDL schema, then for each
    //   entry build a ProviderLabel with pid = resolve(name). Placing the row by
    //   pid needs the same pid<->name attribution the module labels need — both
    //   wait on the attribution decision (DESIGN "Process-tree attribution").
    return labels;
}
