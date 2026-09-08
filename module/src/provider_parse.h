#pragma once

// Collector B, the pure half — turn a connection_source provider's payload into
// ProviderLabels. No SDK, no I/O; the SDK only *fetches* the payload (the bound
// wrapper's collectConnections() call in netgraph_impl), then hands the JSON here.
// Factoring it out keeps the untrusted-feed validation unit-testable and keeps
// the "one bad provider never breaks a sweep" guarantee in tested code.
//
// The payload shape is interfaces/connection_source.h:
//   { "connections": [ { remote:{addr,port,host?}, local:{port}?, peer_id?,
//                        network?, transport?, direction?, derived? }, ... ] }
//
// Validation is deliberately strict on the match key (a row with no usable
// remote endpoint cannot be merged and is dropped) and lenient on labels (an
// unknown transport/direction is ignored, not fatal). A malformed entry is
// skipped and counted; a malformed top-level shape yields no labels.

#include <cstdint>
#include <string>
#include <vector>

#include "merge.h"        // netgraph::ProviderLabel

#include <logos_json.h>   // LogosMap

namespace netgraph {

struct ProviderParseResult {
    std::vector<ProviderLabel> labels;
    int  skipped = 0;          // entries rejected as malformed
    bool validShape = true;    // false when top-level {"connections":[...]} is absent
};

// Parse one provider's payload. `module` is the provider's own module name
// (authoritative label) and `pid` is that provider's resolved pid (0 if the
// resolver could not attribute it — the label still enriches by remote/local,
// but pid-keyed socket matching will not fire, which is correct).
ProviderParseResult parseConnectionSourcePayload(const std::string& module,
                                                 int64_t pid,
                                                 const LogosMap& payload);

}  // namespace netgraph
