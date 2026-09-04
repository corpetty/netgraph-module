#pragma once

// PURE parsing of Linux /proc/net/{tcp,tcp6,udp,udp6} into SocketRows. No
// filesystem access and no Logos SDK here — this is the error-prone half
// (hex/endian address decode, state mapping, inode extraction) isolated so it
// can be unit-tested on any host against captured fixtures.
//
// The filesystem read and the inode->pid mapping live in linux_socket_table.cpp;
// this file only turns already-read text into rows.

#include <cstdint>
#include <string>
#include <vector>

#include "collector.h"  // netgraph::SocketRow, Endpoint

namespace netgraph {

// Which /proc/net file the text came from — fixes address family and transport.
enum class ProcNetKind {
    Tcp4,
    Tcp6,
    Udp4,
    Udp6,
};

// Parse the full contents of one /proc/net/{tcp,tcp6,udp,udp6} file. The header
// line and blank/short lines are skipped. `pid`, `direction` and (for the join)
// nothing else are set here: rows come back with pid=0 and direction empty;
// the socket table fills pid from the inode map and direction is assigned by
// inferDirections() once every row in the sweep is known.
std::vector<SocketRow> parseProcNet(ProcNetKind kind, const std::string& contents);

// Decode a /proc/net hex address token ("0100007F:1F90" for v4,
// "…32 hex…:1F90" for v6) into a presentation address + port. Exposed for tests.
Endpoint parseHexEndpoint(ProcNetKind kind, const std::string& token);

// Map the /proc/net TCP state hex ("0A") to a normalized state string
// ("listen", "established", "closing", …). UDP has no real states; callers pass
// isUdp=true to collapse to "established" (connected) at the row level — the
// remote presence is what actually distinguishes a connected UDP socket.
std::string normalizeState(const std::string& hexState, bool isUdp);

// Second pass over a full sweep's rows: a non-listen row whose local port is one
// of the sweep's listening ports is inbound; otherwise outbound. Listen rows
// keep direction "listen". Mutates in place. Pure (no I/O), unit-tested.
void inferDirections(std::vector<SocketRow>& rows);

}  // namespace netgraph
