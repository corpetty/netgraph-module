#include "attribution.h"

namespace netgraph {

namespace {

// Read a pid from a stats entry: accept a number or a numeric string.
bool readPid(const LogosMap& e, int64_t& pid) {
    if (!e.contains("pid")) return false;
    const auto& p = e["pid"];
    if (p.is_number_integer())      pid = p.get<int64_t>();
    else if (p.is_number_unsigned()) pid = static_cast<int64_t>(p.get<uint64_t>());
    else if (p.is_string()) { try { pid = std::stoll(p.get<std::string>()); } catch (...) { return false; } }
    else return false;
    return pid > 0;
}

const LogosMap* firstArray(const LogosMap& stats) {
    if (stats.is_array()) return &stats;
    if (stats.is_object()) {
        // Prefer an explicit "modules"/"stats" key, else the first array value.
        for (const char* k : {"modules", "stats", "result"})
            if (stats.contains(k) && stats[k].is_array()) return &stats[k];
        for (auto it = stats.begin(); it != stats.end(); ++it)
            if (it->is_array()) return &(*it);
    }
    return nullptr;
}

Endpoint endpointFrom(const LogosMap& m) {
    Endpoint e;
    if (m.contains("addr") && m["addr"].is_string()) e.addr = m["addr"].get<std::string>();
    if (m.contains("port") && m["port"].is_number()) e.port = static_cast<uint16_t>(m.value("port", 0));
    return e;
}

std::string strField(const LogosMap& m, const char* k) {
    return (m.contains(k) && m[k].is_string()) ? m[k].get<std::string>() : std::string();
}

}  // namespace

std::unordered_map<int64_t, std::string> parseModuleStats(const LogosMap& stats) {
    std::unordered_map<int64_t, std::string> out;
    const LogosMap* arr = firstArray(stats);
    if (!arr) return out;
    for (const auto& e : *arr) {
        if (!e.is_object()) continue;
        int64_t pid = 0;
        if (!readPid(e, pid)) continue;
        const std::string name = strField(e, "name");
        if (name.empty()) continue;
        out[pid] = name;
    }
    return out;
}

std::vector<ProviderLabel> parseProviderConnections(const std::string& moduleName,
                                                    int64_t pid,
                                                    const LogosMap& payload) {
    std::vector<ProviderLabel> out;
    if (!payload.contains("connections") || !payload["connections"].is_array()) return out;

    for (const auto& c : payload["connections"]) {
        if (!c.is_object() || !c.contains("remote") || !c["remote"].is_object()) continue;
        ProviderLabel l;
        l.module = moduleName;
        l.pid = pid;
        l.remote = endpointFrom(c["remote"]);
        // A label with no remote endpoint can't key onto a socket; skip unless it
        // is explicitly a derived edge (which carries its own identity).
        const bool isDerived = c.value("derived", false);
        if (l.remote.addr.empty() && !isDerived) continue;

        l.host      = strField(c["remote"], "host");
        l.network   = strField(c, "network");
        l.peerId    = strField(c, "peer_id");
        l.transport = strField(c, "transport");
        l.direction = strField(c, "direction");
        l.derived   = isDerived;

        if (c.contains("local") && c["local"].is_object() &&
            c["local"].contains("port") && c["local"]["port"].is_number()) {
            l.hasLocalPort = true;
            l.localPort = static_cast<uint16_t>(c["local"].value("port", 0));
        }
        out.push_back(std::move(l));
    }
    return out;
}

}  // namespace netgraph
