// Pure unit tests for Collector B's payload parser: a valid connection_source
// feed becomes ProviderLabels; a missing/invalid remote drops the entry; local
// port presence sets the wide match key; unknown transport/direction hints are
// ignored not fatal; a malformed top-level shape yields no labels; and — the
// core guarantee — a garbage entry never throws, it is skipped and counted.

#include <cassert>
#include <cstdio>
#include <string>

#include "provider_parse.h"

using namespace netgraph;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    // A well-formed feed with a mix of shapes.
    LogosMap payload = LogosMap::parse(R"({
      "connections": [
        { "remote": {"addr":"1.2.3.4","port":9000,"host":"seed.example"},
          "local": {"port":53086},
          "peer_id":"PZxPeer", "network":"chain-p2p",
          "transport":"quic", "direction":"outbound" },
        { "remote": {"addr":"6.6.6.7","port":1234},
          "network":"mix-overlay", "peer_id":"MixPeer", "derived":true },
        { "remote": {"addr":"9.9.9.9","port":443}, "transport":"bogus",
          "direction":"sideways" },
        { "local": {"port":80} },
        { "remote": {"addr":"7.7.7.7"} },
        { "remote": {"addr":"8.8.8.8","port":70000} },
        "not-an-object",
        { "remote": {"addr":"5.5.5.5","port":22} , "peer_id": 12345 }
      ]
    })");

    ProviderParseResult r = parseConnectionSourcePayload("blockchain_module", 100, payload);
    CHECK(r.validShape);

    // Valid rows: entries 0, 1, 2, and 7 (peer_id wrong type is tolerated -> "").
    // Dropped: entry 3 (no remote), 4 (remote has no port), 5 (port out of range),
    // 6 (not an object).
    CHECK(r.labels.size() == 4);
    CHECK(r.skipped == 4);

    // Entry 0: full label, wide key (local port present), quic hint kept.
    const ProviderLabel& a = r.labels[0];
    CHECK(a.module == "blockchain_module");
    CHECK(a.pid == 100);
    CHECK(a.remote.addr == "1.2.3.4");
    CHECK(a.remote.port == 9000);
    CHECK(a.host == "seed.example");
    CHECK(a.hasLocalPort);
    CHECK(a.localPort == 53086);
    CHECK(a.peerId == "PZxPeer");
    CHECK(a.network == "chain-p2p");
    CHECK(a.transport == "quic");
    CHECK(a.direction == "outbound");
    CHECK(!a.derived);

    // Entry 1: derived edge, no local port -> loose key.
    const ProviderLabel& b = r.labels[1];
    CHECK(b.derived);
    CHECK(!b.hasLocalPort);
    CHECK(b.remote.addr == "6.6.6.7");
    CHECK(b.network == "mix-overlay");

    // Entry 2: unknown transport/direction hints dropped, row still valid.
    const ProviderLabel& c = r.labels[2];
    CHECK(c.remote.addr == "9.9.9.9");
    CHECK(c.transport.empty());
    CHECK(c.direction.empty());

    // Entry 7: wrong-typed peer_id tolerated as empty, not a throw.
    const ProviderLabel& d = r.labels[3];
    CHECK(d.remote.addr == "5.5.5.5");
    CHECK(d.peerId.empty());

    // pid=0 (unresolved provider) still parses; the label just won't pid-match.
    ProviderParseResult unattr = parseConnectionSourcePayload("x", 0, payload);
    CHECK(unattr.labels.size() == 4);
    CHECK(unattr.labels[0].pid == 0);

    // Malformed top-level shape -> no labels, flagged.
    ProviderParseResult bad1 = parseConnectionSourcePayload("x", 1, LogosMap::parse(R"({"nope":[]})"));
    CHECK(!bad1.validShape);
    CHECK(bad1.labels.empty());

    ProviderParseResult bad2 = parseConnectionSourcePayload("x", 1, LogosMap::parse(R"([1,2,3])"));
    CHECK(!bad2.validShape);

    ProviderParseResult empty = parseConnectionSourcePayload("x", 1, LogosMap::parse(R"({"connections":[]})"));
    CHECK(empty.validShape);
    CHECK(empty.labels.empty());
    CHECK(empty.skipped == 0);

    if (failures == 0) std::printf("provider_parse_test: OK\n");
    else std::printf("provider_parse_test: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
