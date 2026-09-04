// Pure unit tests for the A/B merge: socket rows are authoritative for
// existence, provider rows add labels only, an unmatched provider row is a
// derived edge, unlabelled rows keep null labels, host pids are tagged.
// Needs a LogosMap (nlohmann::json) — the SDK provides logos_json.h; a local
// shim provides it when building these outside the builder (see tests/README).

#include <cassert>
#include <cstdio>
#include <string>

#include "merge.h"

using namespace netgraph;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static LogosMap findByRemoteAddr(const LogosMap& arr, const std::string& addr) {
    for (const auto& r : arr)
        if (r.contains("remote") && r["remote"].value("addr", "") == addr) return r;
    return LogosMap();
}

int main() {
    // Three sockets on pid 100: a labelled chain peer, an unlabelled DNS lookup,
    // and (host) a socket on pid 999.
    std::vector<SocketRow> sockets;
    {
        SocketRow chain;
        chain.pid = 100; chain.transport = "tcp"; chain.direction = "outbound";
        chain.local = {"10.0.0.2", 53086}; chain.remote = {"1.2.3.4", 9000};
        chain.state = "established";
        sockets.push_back(chain);

        SocketRow dns;
        dns.pid = 100; dns.transport = "udp"; dns.direction = "outbound";
        dns.local = {"10.0.0.2", 45999}; dns.remote = {"8.8.8.8", 53};
        dns.state = "established";
        sockets.push_back(dns);

        SocketRow host;
        host.pid = 999; host.transport = "tcp"; host.direction = "inbound";
        host.local = {"10.0.0.2", 443}; host.remote = {"5.5.5.5", 51000};
        host.state = "established";
        sockets.push_back(host);
    }

    // Provider labels: one matches the chain socket (no local port -> loose key),
    // one is a derived edge (relayed, no socket).
    std::vector<ProviderLabel> labels;
    {
        ProviderLabel chain;
        chain.module = "blockchain_module"; chain.pid = 100;
        chain.hasLocalPort = false; chain.remote = {"1.2.3.4", 9000};
        chain.network = "chain-p2p"; chain.peerId = "PZxPeer";
        chain.transport = "quic"; chain.host = "seed.chain.example";
        labels.push_back(chain);

        ProviderLabel relay;
        relay.module = "mix_module"; relay.pid = 100;
        relay.remote = {"6.6.6.7", 1234}; relay.network = "mix-overlay";
        relay.peerId = "MixPeer"; relay.derived = true;
        labels.push_back(relay);
    }

    MergeContext ctx;
    ctx.hostPids = {999};
    ctx.pidNames = {{100, "blockchain_module"}};  // pid->name attribution base

    LogosMap out = mergeConnections(sockets, labels, ctx);
    CHECK(out.is_array());
    CHECK(out.size() == 4);  // 3 sockets + 1 derived

    // Labelled chain socket: provider fields win, transport refined tcp->quic.
    LogosMap chain = findByRemoteAddr(out, "1.2.3.4");
    CHECK(!chain.is_null());
    CHECK(chain["module"] == "blockchain_module");
    CHECK(chain["network"] == "chain-p2p");
    CHECK(chain["peer_id"] == "PZxPeer");
    CHECK(chain["transport"] == "quic");
    CHECK(chain["direction"] == "outbound");        // from socket (label gave none)
    CHECK(chain["remote"]["host"] == "seed.chain.example");
    CHECK(chain["host"] == false);
    CHECK(!chain.contains("derived"));
    CHECK(chain["opened_at"].is_null());

    // DNS socket with no provider label: module comes from pid->name attribution
    // (same pid 100 = blockchain_module), but network/peer_id stay null and
    // transport is the socket's.
    LogosMap dns = findByRemoteAddr(out, "8.8.8.8");
    CHECK(!dns.is_null());
    CHECK(dns["module"] == "blockchain_module");
    CHECK(dns["network"].is_null());
    CHECK(dns["peer_id"].is_null());
    CHECK(dns["transport"] == "udp");

    // Host socket: tagged host:true.
    LogosMap host = findByRemoteAddr(out, "5.5.5.5");
    CHECK(!host.is_null());
    CHECK(host["host"] == true);
    CHECK(host["module"].is_null());

    // Derived edge: marked, module label present, existence not from a socket.
    LogosMap relay = findByRemoteAddr(out, "6.6.6.7");
    CHECK(!relay.is_null());
    CHECK(relay["derived"] == true);
    CHECK(relay["module"] == "mix_module");
    CHECK(relay["local"]["addr"].is_null());

    // Stable id shape: 16 hex chars, deterministic.
    std::string id1 = connectionId(100, 53086, {"1.2.3.4", 9000});
    std::string id2 = connectionId(100, 53086, {"1.2.3.4", 9000});
    CHECK(id1.size() == 16);
    CHECK(id1 == id2);
    CHECK(connectionId(100, 53087, {"1.2.3.4", 9000}) != id1);

    if (failures == 0) std::printf("merge_test: OK\n");
    else std::printf("merge_test: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
