#include "merge.h"

#include <cstdint>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace netgraph {

namespace {

std::string endpointKey(const Endpoint& e) {
    std::ostringstream os;
    os << e.addr << ':' << e.port;
    return os.str();
}

// Full key: pid | localPort | remote. Used when a provider gave a local port and
// for the socket-row index.
std::string fullKey(int64_t pid, uint16_t localPort, const Endpoint& remote) {
    std::ostringstream os;
    os << pid << '|' << localPort << '|' << endpointKey(remote);
    return os.str();
}

// Loose key: pid | remote. Used when a provider gave no local port.
std::string looseKey(int64_t pid, const Endpoint& remote) {
    std::ostringstream os;
    os << pid << '|' << endpointKey(remote);
    return os.str();
}

// A 64-bit FNV-1a of the id inputs, hex-encoded. Stable within a run; cheap and
// dependency-free. Not cryptographic — it only has to disambiguate live rows.
std::string fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

}  // namespace

std::string connectionId(int64_t pid, uint16_t localPort, const Endpoint& remote) {
    std::ostringstream os;
    os << pid << '|' << localPort << '|' << endpointKey(remote);
    return fnv1a(os.str());
}

LogosMap mergeConnections(const std::vector<SocketRow>& sockets,
                          const std::vector<ProviderLabel>& labels,
                          const MergeContext& ctx) {
    std::unordered_set<int64_t> hostPids(ctx.hostPids.begin(), ctx.hostPids.end());

    // Index socket rows by both keys so a provider label can find its socket
    // whether or not it reported a local port. Value = index into `sockets`.
    std::unordered_map<std::string, size_t> byFull;
    std::unordered_multimap<std::string, size_t> byLoose;
    for (size_t i = 0; i < sockets.size(); ++i) {
        const auto& s = sockets[i];
        byFull.emplace(fullKey(s.pid, s.local.port, s.remote), i);
        byLoose.emplace(looseKey(s.pid, s.remote), i);
    }

    // For each socket row, the label that enriched it (if any). One label per
    // socket — first match wins; extra labels for the same socket are ignored on
    // the socket but do not spawn derived edges.
    std::vector<const ProviderLabel*> socketLabel(sockets.size(), nullptr);
    std::vector<const ProviderLabel*> derived;

    for (const auto& lbl : labels) {
        if (lbl.derived) { derived.push_back(&lbl); continue; }

        size_t idx = SIZE_MAX;
        if (lbl.hasLocalPort) {
            auto it = byFull.find(fullKey(lbl.pid, lbl.localPort, lbl.remote));
            if (it != byFull.end()) idx = it->second;
        } else {
            auto range = byLoose.equal_range(looseKey(lbl.pid, lbl.remote));
            if (range.first != range.second) idx = range.first->second;
        }

        if (idx == SIZE_MAX) {
            derived.push_back(&lbl);      // no matching socket => derived edge
        } else if (!socketLabel[idx]) {
            socketLabel[idx] = &lbl;
        }
    }

    LogosMap out = LogosMap::array();

    // Base pid -> module name attribution (from the process source). A provider
    // label refines it; absent both, module stays null.
    auto moduleFor = [&](int64_t pid, const ProviderLabel* lbl) -> LogosMap {
        if (lbl && !lbl->module.empty()) return LogosMap(lbl->module);
        auto it = ctx.pidNames.find(pid);
        if (it != ctx.pidNames.end() && !it->second.empty()) return LogosMap(it->second);
        return LogosMap(nullptr);
    };

    auto putEndpoint = [](const Endpoint& e, const std::string& host) {
        LogosMap m;
        m["addr"] = e.addr;
        m["port"] = e.port;
        if (!host.empty()) m["host"] = host; else m["host"] = nullptr;
        return m;
    };

    // Socket rows: authoritative existence, optionally labelled.
    for (size_t i = 0; i < sockets.size(); ++i) {
        const auto& s = sockets[i];
        const ProviderLabel* lbl = socketLabel[i];

        LogosMap rec;
        rec["id"] = connectionId(s.pid, s.local.port, s.remote);
        rec["pid"] = s.pid;
        rec["module"]  = moduleFor(s.pid, lbl);
        rec["network"] = (lbl && !lbl->network.empty()) ? LogosMap(lbl->network) : LogosMap(nullptr);
        // transport: socket table says tcp/udp; a provider may refine udp->quic.
        rec["transport"] = (lbl && !lbl->transport.empty()) ? lbl->transport : s.transport;
        rec["direction"] = (lbl && !lbl->direction.empty()) ? lbl->direction : s.direction;
        rec["local"]  = putEndpoint(s.local, std::string());
        rec["remote"] = putEndpoint(s.remote, lbl ? lbl->host : std::string());
        rec["peer_id"] = (lbl && !lbl->peerId.empty()) ? LogosMap(lbl->peerId) : LogosMap(nullptr);
        rec["state"] = s.state;
        rec["opened_at"] = nullptr;  // M0: socket table gives no age; first-seen is M2
        rec["bytes"] = nullptr;      // M0: no byte counters from /proc/net basic rows
        rec["host"] = hostPids.count(s.pid) > 0;
        out.push_back(std::move(rec));
    }

    // Derived edges: a module reported a connection with no matching socket
    // (relayed, or multiplexed over one socket). Kept and marked, never merged.
    for (const ProviderLabel* lbl : derived) {
        LogosMap rec;
        rec["id"] = connectionId(lbl->pid, lbl->hasLocalPort ? lbl->localPort : 0, lbl->remote);
        rec["pid"] = lbl->pid;
        rec["module"]  = moduleFor(lbl->pid, lbl);
        rec["network"] = lbl->network.empty() ? LogosMap(nullptr) : LogosMap(lbl->network);
        rec["transport"] = lbl->transport.empty() ? LogosMap(nullptr) : LogosMap(lbl->transport);
        rec["direction"] = lbl->direction.empty() ? LogosMap(nullptr) : LogosMap(lbl->direction);
        LogosMap local;
        local["addr"] = nullptr;
        local["port"] = lbl->hasLocalPort ? LogosMap(lbl->localPort) : LogosMap(nullptr);
        rec["local"] = std::move(local);
        rec["remote"] = putEndpoint(lbl->remote, lbl->host);
        rec["peer_id"] = lbl->peerId.empty() ? LogosMap(nullptr) : LogosMap(lbl->peerId);
        rec["state"] = "established";
        rec["opened_at"] = nullptr;
        rec["bytes"] = nullptr;
        rec["host"] = hostPids.count(lbl->pid) > 0;
        rec["derived"] = true;
        out.push_back(std::move(rec));
    }

    return out;
}

}  // namespace netgraph
