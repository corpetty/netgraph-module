#pragma once

// PURE parsers for the two SDK-fed inputs, factored out of netgraph_impl so they
// are unit-tested without the SDK: (1) the core_service getModuleStats() array
// -> pid->name map (the attribution path chosen for M0: core_service under the
// logoscore daemon now; a stats-exporting core module for Basecamp production),
// and (2) a connection_source provider's collectConnections() payload -> the
// ProviderLabel rows the merge consumes.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "merge.h"        // netgraph::ProviderLabel
#include <logos_json.h>   // LogosMap

namespace netgraph {

// Parse the getModuleStats() payload into pid -> module name. Accepts either a
// bare array [ {name,pid,...}, ... ] or an object { "modules": [...] } / any
// object with an array value — core hosts have varied the envelope. Entries
// without a positive pid or a name are skipped.
std::unordered_map<int64_t, std::string> parseModuleStats(const LogosMap& stats);

// Parse one provider's collectConnections() payload into ProviderLabel rows,
// stamped with the provider's module name and resolved pid. Malformed entries
// are skipped (never throw into a sweep). This is the structural guard; the
// full CDDL validation the handoff wants is layered on top at M3.
std::vector<ProviderLabel> parseProviderConnections(const std::string& moduleName,
                                                    int64_t pid,
                                                    const LogosMap& payload);

}  // namespace netgraph
