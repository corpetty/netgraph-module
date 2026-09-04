#pragma once

// PURE merge of Collector A (socket table) with Collector B (module labels) into
// the connection records snapshot() emits. No I/O, no SDK — unit-tested.
//
// Rules (from the handoff):
//   - A socket-table row is the authority for a connection's EXISTENCE.
//   - A module row supplies LABELS ONLY (module, network, peer_id, host hint,
//     transport refinement e.g. quic); it never creates a socket row.
//   - Match key: (pid, local port, remote endpoint). A module row that provides
//     no local port matches on (pid, remote endpoint).
//   - A module row with NO matching socket is kept as a DERIVED edge, marked
//     derived:true — never silently merged into a socket row.
//   - module / network / peer_id are nullable BY DESIGN; an unlabelled row is
//     shown, never dropped. The unlabelled rows are the point.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "collector.h"  // netgraph::SocketRow

#include <logos_json.h>  // LogosMap

namespace netgraph {

// One label record a connection_source provider returned, already associated
// with the provider's module name and resolved pid. `hasLocalPort` distinguishes
// "local port 0 was reported" from "no local port reported" (match-key scope).
struct ProviderLabel {
    std::string module;        // the provider module name (authoritative label)
    int64_t     pid = 0;       // provider's pid, resolved from process stats
    bool        hasLocalPort = false;
    uint16_t    localPort = 0;
    Endpoint    remote;        // remote addr:port (host optional in .addr? no — see host)
    std::string host;          // reverse/SNI host, optional
    std::string network;       // provider classification, optional
    std::string peerId;        // optional
    std::string transport;     // optional refinement ("quic")
    std::string direction;     // optional
    bool        derived = false;  // provider says this is a relayed/muxed edge
};

// Per-sweep attribution context.
//   pidNames  base pid -> module name (from the process source's NameResolver;
//             empty in Basecamp until the attribution path lands — see DESIGN).
//             A connection_source provider label still refines on top of this.
//   hostPids  pids to tag host:true so the UI can filter host noise; inclusion
//             itself is controlled upstream by setEnabled's include_host.
struct MergeContext {
    std::unordered_map<int64_t, std::string> pidNames;
    std::vector<int64_t> hostPids;
};

// Merge socket rows (already pid-attributed and direction-inferred) with
// provider labels. Returns a LogosMap array of connection records in the handoff
// shape. `openMs` is the sweep timestamp used only for the document, not per row.
LogosMap mergeConnections(const std::vector<SocketRow>& sockets,
                          const std::vector<ProviderLabel>& labels,
                          const MergeContext& ctx);

// The stable per-connection id: hash of pid | local port | remote endpoint.
// Ephemeral by design (pids/ports recycle across runs) — a live-view identity,
// not a durable one.
std::string connectionId(int64_t pid, uint16_t localPort, const Endpoint& remote);

}  // namespace netgraph
