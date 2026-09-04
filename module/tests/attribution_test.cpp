// Pure tests for the SDK-fed input parsers: core_service getModuleStats() ->
// pid->name, and a connection_source collectConnections() payload -> labels.
// Needs a LogosMap; no SDK.

#include <cstdio>
#include <string>

#include "attribution.h"

using namespace netgraph;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    // --- parseModuleStats: bare array, numeric and string pids -------------
    {
        LogosMap arr = LogosMap::parse(R"([
          {"name":"blockchain_module","pid":1234,"cpu_percent":1.2,"memory_mb":40},
          {"name":"mix_module","pid":"1235"},
          {"name":"","pid":9},
          {"pid":10},
          {"name":"host_process","pid":0}
        ])");
        auto m = parseModuleStats(arr);
        CHECK(m.size() == 2);
        CHECK(m[1234] == "blockchain_module");
        CHECK(m[1235] == "mix_module");     // numeric string pid accepted
        CHECK(m.count(9) == 0);             // empty name skipped
        CHECK(m.count(0) == 0);             // non-positive pid skipped
    }
    // --- parseModuleStats: object envelope --------------------------------
    {
        LogosMap obj = LogosMap::parse(R"({"modules":[{"name":"a","pid":5}]})");
        auto m = parseModuleStats(obj);
        CHECK(m.size() == 1 && m[5] == "a");
    }

    // --- parseProviderConnections -----------------------------------------
    {
        LogosMap payload = LogosMap::parse(R"({"connections":[
          { "remote":{"addr":"1.2.3.4","port":9000,"host":"seed.example"},
            "network":"chain-p2p", "peer_id":"PeerZ", "transport":"quic",
            "local":{"port":40001} },
          { "remote":{"addr":"6.6.6.7","port":1234}, "network":"mix-overlay",
            "peer_id":"MixPeer", "derived":true },
          { "network":"no-remote" },
          { "remote":{"addr":"","port":0}, "derived":true, "peer_id":"Muxed" }
        ]})");
        // Row 3 ({"network":"no-remote"}) is dropped: no remote and not derived.
        auto labels = parseProviderConnections("blockchain_module", 1234, payload);
        CHECK(labels.size() == 3);   // two with remotes + one derived-no-remote

        // first: full label with local port.
        const ProviderLabel& a = labels[0];
        CHECK(a.module == "blockchain_module");
        CHECK(a.pid == 1234);
        CHECK(a.remote.addr == "1.2.3.4" && a.remote.port == 9000);
        CHECK(a.host == "seed.example");
        CHECK(a.network == "chain-p2p" && a.peerId == "PeerZ" && a.transport == "quic");
        CHECK(a.hasLocalPort && a.localPort == 40001);
        CHECK(!a.derived);

        // second: derived edge with a remote.
        CHECK(labels[1].derived && labels[1].network == "mix-overlay");
        CHECK(!labels[1].hasLocalPort);

        // third: derived edge with no remote is kept (carries its own identity).
        CHECK(labels[2].derived && labels[2].peerId == "Muxed");
    }
    // --- malformed payloads never throw -----------------------------------
    {
        CHECK(parseProviderConnections("m", 1, LogosMap::parse(R"({})")).empty());
        CHECK(parseProviderConnections("m", 1, LogosMap::parse(R"({"connections":5})")).empty());
        CHECK(parseProviderConnections("m", 1, LogosMap::parse(R"([])")).empty());
    }

    if (failures == 0) std::printf("attribution_test: OK\n");
    else std::printf("attribution_test: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
