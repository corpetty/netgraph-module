#pragma once

// A DEPENDENCY INTERFACE — the contract a module satisfies to have its own
// connections labelled by the netgraph observer. It names NO concrete module:
// any module whose own API includes a matching `collectConnections()` satisfies
// it (the superset rule, same as openmetrics' metrics_source). netgraph binds it
// to operator-chosen module names at runtime via
// modules().bind_connection_source("some_module").
//
// The Logos generator parses this file and emits a BOUND wrapper class
// `ConnectionSource` whose target module name is a runtime ctor argument.
//
// Types are std / LogosMap because the consuming module is interface:
// "universal" — the bound wrapper inherits that api-style.
//
// This is Collector B (enrichment). It supplies LABELS ONLY. It never creates a
// row: the socket table (Collector A) is the sole authority for a connection's
// existence. A module row is matched to a socket row on (pid, local port, remote
// endpoint); where a module reports a connection with no matching socket
// (relayed, or multiplexed over one socket), netgraph keeps it as a DERIVED edge
// and marks it as such — it is not silently merged into a socket row.
//
// Provider payload shape (all fields but the match key are optional; a provider
// reports only what it knows):
//
//   {
//     "connections": [
//       {
//         "remote":    { "addr": "1.2.3.4", "port": 9000, "host": "..." },
//         "local":     { "port": 0 },          // omit if unknown
//         "peer_id":   "<multibase>",          // the label that matters
//         "network":   "mix-overlay",          // provider's own classification
//         "transport": "quic" | "tcp" | "udp", // hint only; socket table wins
//         "direction": "outbound" | "inbound",
//         "derived":   false                   // true => relayed/muxed, no socket
//       }
//     ]
//   }
//
// A module that does not implement collectConnections() is skipped; one broken
// or slow provider must never break a sweep (same guarantee openmetrics gives a
// scrape). The netgraph consumer validates this payload against a CDDL schema
// before merging (M3) and rejects a malformed feed rather than rendering it.

#include <string>

#include <logos_json.h>            // LogosMap (nlohmann::json alias)
#include <logos_module_context.h>  // defines the `logos_events` token

class IConnectionSource {
public:
    // Return a {"connections": [ { remote, local?, peer_id?, network?,
    // transport?, direction?, derived? }, ... ]} payload labelling the calling
    // module's own connections. Labels only — see the header note above.
    LogosMap collectConnections();
};
