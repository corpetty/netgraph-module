// Fakes for the socket table and the pid source, used by unit tests and the M0
// doctest when a scripted tree is wanted instead of the live host.
//
//   NETGRAPH_FAKE_SOCKETS = path to a JSON file:
//     { "sockets": [ { "pid":123, "transport":"tcp", "direction":"outbound",
//                      "state":"established",
//                      "local":  {"addr":"10.0.0.2","port":53086},
//                      "remote": {"addr":"1.2.3.4","port":9000},
//                      "inode": 0 }, ... ] }
//     (enumerate() returns the rows whose pid is in the requested set.)
//
//   NETGRAPH_FAKE_PIDS = "1234:blockchain_module,1235:mix_module,999:*host"
//     comma list of pid[:name][ and a leading * marks the host process ].

#include "collector.h"
#include "process_source.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <logos_json.h>  // LogosMap

namespace netgraph {

namespace {

Endpoint endpointFrom(const LogosMap& m) {
    Endpoint e;
    if (m.contains("addr") && m["addr"].is_string()) e.addr = m["addr"].get<std::string>();
    if (m.contains("port")) e.port = static_cast<uint16_t>(m.value("port", 0));
    return e;
}

class FakeSocketTable : public ISocketTable {
public:
    explicit FakeSocketTable(std::vector<SocketRow> rows) : m_rows(std::move(rows)) {}
    std::vector<SocketRow> enumerate(const std::vector<int64_t>& pids) override {
        std::unordered_set<int64_t> want(pids.begin(), pids.end());
        std::vector<SocketRow> out;
        for (const auto& r : m_rows)
            if (want.count(r.pid)) out.push_back(r);
        return out;
    }
private:
    std::vector<SocketRow> m_rows;
};

class FakeProcessSource : public IProcessSource {
public:
    explicit FakeProcessSource(std::vector<ProcInfo> procs) : m_procs(std::move(procs)) {}
    std::vector<ProcInfo> processes(bool includeHost) override {
        std::vector<ProcInfo> out;
        for (const auto& p : m_procs)
            if (includeHost || !p.host) out.push_back(p);
        return out;
    }
private:
    std::vector<ProcInfo> m_procs;
};

}  // namespace

std::unique_ptr<ISocketTable> makeFakeSocketTable(const char* path) {
    std::vector<SocketRow> rows;
    std::ifstream f(path ? path : "");
    if (f) {
        try {
            LogosMap doc = LogosMap::parse(f);
            if (doc.contains("sockets") && doc["sockets"].is_array()) {
                for (const auto& s : doc["sockets"]) {
                    SocketRow r;
                    r.pid = s.value("pid", 0);
                    r.transport = s.value("transport", "tcp");
                    r.direction = s.value("direction", "");
                    r.state = s.value("state", "established");
                    r.inode = s.value("inode", 0);
                    if (s.contains("local"))  r.local  = endpointFrom(s["local"]);
                    if (s.contains("remote")) r.remote = endpointFrom(s["remote"]);
                    rows.push_back(std::move(r));
                }
            }
        } catch (...) { /* malformed fixture => empty table */ }
    }
    return std::unique_ptr<ISocketTable>(new FakeSocketTable(std::move(rows)));
}

std::unique_ptr<IProcessSource> makeFakeProcessSource(const char* spec) {
    std::vector<ProcInfo> procs;
    std::string s = spec ? spec : "";
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        ProcInfo p;
        const auto colon = item.find(':');
        std::string pidStr = colon == std::string::npos ? item : item.substr(0, colon);
        std::string name = colon == std::string::npos ? "" : item.substr(colon + 1);
        if (!name.empty() && name[0] == '*') { p.host = true; name = name.substr(1); }
        try { p.pid = std::stoll(pidStr); } catch (...) { continue; }
        p.name = name;
        procs.push_back(std::move(p));
    }
    return std::unique_ptr<IProcessSource>(new FakeProcessSource(std::move(procs)));
}

}  // namespace netgraph
