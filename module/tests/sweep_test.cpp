// End-to-end test of the pure sweep pipeline against the fakes: a scripted
// process tree + socket table + one provider label produce the snapshot
// document. Proves discovery -> enumerate -> merge -> document wiring, including
// host tagging, pid->name attribution, and that Collector A alone (no labels)
// still yields rows.

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "sweep.h"

using namespace netgraph;

namespace netgraph {
std::unique_ptr<ISocketTable>   makeFakeSocketTable(const char* path);
std::unique_ptr<IProcessSource> makeFakeProcessSource(const char* spec);
}

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    // Scripted socket fixture written to a temp file for the fake table.
    const char* fixture = "/tmp/netgraph_sweep_fixture.json";
    {
        FILE* f = std::fopen(fixture, "w");
        std::fputs(R"({ "sockets": [
          { "pid":1234, "transport":"tcp", "direction":"outbound", "state":"established",
            "local":{"addr":"10.0.0.2","port":40001}, "remote":{"addr":"1.2.3.4","port":9000}, "inode":11 },
          { "pid":1234, "transport":"udp", "direction":"outbound", "state":"established",
            "local":{"addr":"10.0.0.2","port":40002}, "remote":{"addr":"8.8.8.8","port":53}, "inode":12 },
          { "pid":999, "transport":"tcp", "direction":"inbound", "state":"established",
            "local":{"addr":"10.0.0.2","port":443}, "remote":{"addr":"5.5.5.5","port":51000}, "inode":13 }
        ] })", f);
        std::fclose(f);
    }

    auto psrc = makeFakeProcessSource("1234:blockchain_module,999:*host_process");
    auto tbl  = makeFakeSocketTable(fixture);
    CHECK(psrc && tbl);

    // One provider label enriching the chain socket with peer_id + network.
    std::vector<ProviderLabel> labels;
    {
        ProviderLabel l;
        l.module = "blockchain_module"; l.pid = 1234;
        l.remote = {"1.2.3.4", 9000}; l.network = "chain-p2p"; l.peerId = "PeerZ";
        labels.push_back(l);
    }

    // includeHost=true: host socket present and tagged.
    std::string out = buildSnapshot(*psrc, *tbl, labels, /*includeHost=*/true,
                                    /*enabled=*/true, /*sweptAtMs=*/1725480000000LL);
    LogosMap doc = LogosMap::parse(out);
    CHECK(doc["enabled"] == true);
    CHECK(doc["swept_at"] == 1725480000000LL);
    CHECK(doc["connections"].is_array());
    CHECK(doc["connections"].size() == 3);

    int host = 0, chain = 0, dns = 0;
    for (const auto& r : doc["connections"]) {
        const std::string addr = r["remote"].value("addr", "");
        if (addr == "5.5.5.5") { host = 1; CHECK(r["host"] == true); CHECK(r["direction"] == "inbound"); }
        if (addr == "1.2.3.4") { chain = 1; CHECK(r["network"] == "chain-p2p"); CHECK(r["peer_id"] == "PeerZ");
                                 CHECK(r["module"] == "blockchain_module"); }
        if (addr == "8.8.8.8") { dns = 1; CHECK(r["module"] == "blockchain_module");  // pid attribution
                                 CHECK(r["network"].is_null()); CHECK(r["transport"] == "udp"); }
    }
    CHECK(host && chain && dns);

    // includeHost=false: host process (and its socket) excluded.
    std::string out2 = buildSnapshot(*psrc, *tbl, {}, /*includeHost=*/false,
                                     /*enabled=*/true, /*sweptAtMs=*/0);
    LogosMap doc2 = LogosMap::parse(out2);
    CHECK(doc2["connections"].size() == 2);  // 1234's two sockets only
    for (const auto& r : doc2["connections"]) CHECK(r["host"] == false);

    // Collector A alone (no labels, no names): rows still present, module null.
    auto plain = makeFakeProcessSource("1234");
    std::string out3 = buildSnapshot(*plain, *tbl, {}, false, true, 0);
    LogosMap doc3 = LogosMap::parse(out3);
    CHECK(doc3["connections"].size() == 2);
    for (const auto& r : doc3["connections"]) CHECK(r["module"].is_null());

    if (failures == 0) std::printf("sweep_test: OK\n");
    else std::printf("sweep_test: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
