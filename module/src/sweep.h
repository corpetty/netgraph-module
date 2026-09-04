#pragma once

// The pure sweep pipeline: process discovery -> socket enumeration -> merge ->
// snapshot document. No SDK, no threading, no I/O of its own beyond what the
// injected seams do — so it runs against the fakes in a unit test and against
// the live host in the module. netgraph_impl supplies the provider labels
// (Collector B, which needs the SDK) and the timer; everything else is here.

#include <cstdint>
#include <string>
#include <vector>

#include "collector.h"        // ISocketTable
#include "process_source.h"   // IProcessSource
#include "merge.h"            // ProviderLabel, mergeConnections

#include <logos_json.h>       // LogosMap

namespace netgraph {

// Run one sweep and return the snapshot document string:
//   { "enabled": bool, "swept_at": <sweptAtMs>, "connections": [ <record>... ] }
// `labels` are the connection_source provider rows for this sweep (empty is
// valid — Collector A alone produces a real graph). includeHost adds and tags
// the host process.
std::string buildSnapshot(IProcessSource& procSource,
                          ISocketTable& sockets,
                          const std::vector<ProviderLabel>& labels,
                          bool includeHost,
                          bool enabled,
                          int64_t sweptAtMs);

}  // namespace netgraph
