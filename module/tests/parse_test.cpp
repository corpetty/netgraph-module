// Pure unit tests for the /proc/net parser: hex/endian address decode, state
// mapping, listen/inbound/outbound inference, and the no-remote handling.
// Framework-free (assert). Compiles with just the C++ stdlib — no Logos SDK.

#include <cassert>
#include <cstdio>
#include <string>

#include "proc_net_parse.h"

using namespace netgraph;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static const SocketRow* findByRemote(const std::vector<SocketRow>& rows,
                                     const std::string& addr, uint16_t port) {
    for (const auto& r : rows)
        if (r.remote.addr == addr && r.remote.port == port) return &r;
    return nullptr;
}

int main() {
    // --- endpoint decode --------------------------------------------------
    {
        Endpoint e = parseHexEndpoint(ProcNetKind::Tcp4, "0100007F:1F90");
        CHECK(e.addr == "127.0.0.1");
        CHECK(e.port == 8080);

        Endpoint z = parseHexEndpoint(ProcNetKind::Tcp4, "00000000:0000");
        CHECK(z.addr == "0.0.0.0");
        CHECK(z.port == 0);

        // ::1 loopback, /proc tcp6 word-order encoding.
        Endpoint v6 = parseHexEndpoint(ProcNetKind::Tcp6,
                                       "00000000000000000000000001000000:0035");
        CHECK(v6.addr == "::1");
        CHECK(v6.port == 53);
    }

    // --- state mapping ----------------------------------------------------
    {
        CHECK(normalizeState("0A", false) == "listen");
        CHECK(normalizeState("01", false) == "established");
        CHECK(normalizeState("06", false) == "closing");   // TIME_WAIT
        CHECK(normalizeState("07", false) == "closed");
        CHECK(normalizeState("zz", false) == "unknown");
        CHECK(normalizeState("07", true) == "established"); // udp: state ignored
    }

    // --- full tcp4 table + direction inference ---------------------------
    {
        const std::string tcp4 =
            "  sl  local_address rem_address   st ...header...\n"
            "   0: 0100007F:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000  0 12345 1 x\n"
            "   1: 0100007F:CF5E 0100007F:1F90 01 00000000:00000000 00:00000000 00000000  1000  0 12346 1 x\n"
            "   2: 0100007F:1F90 0100007F:CF5E 01 00000000:00000000 00:00000000 00000000  1000  0 12347 1 x\n";
        auto rows = parseProcNet(ProcNetKind::Tcp4, tcp4);
        CHECK(rows.size() == 3);
        inferDirections(rows);

        // listen row: no remote, state listen, direction listen, port 8080.
        const SocketRow* listen = nullptr;
        for (const auto& r : rows) if (r.state == "listen") listen = &r;
        CHECK(listen != nullptr);
        CHECK(listen && listen->local.port == 8080);
        CHECK(listen && listen->direction == "listen");
        CHECK(listen && listen->remote.addr.empty());
        CHECK(listen && listen->inode == 12345);

        // client side: local 53086 -> outbound (not a listen port).
        const SocketRow* client = findByRemote(rows, "127.0.0.1", 8080);
        CHECK(client != nullptr);
        CHECK(client && client->direction == "outbound");
        CHECK(client && client->transport == "tcp");

        // server side: local 8080 (a listen port) -> inbound.
        const SocketRow* server = findByRemote(rows, "127.0.0.1", 53086);
        CHECK(server != nullptr);
        CHECK(server && server->direction == "inbound");
    }

    // --- udp: unconnected socket is kept as a listen row, connected is a peer -
    {
        const std::string udp4 =
            "  sl  local_address rem_address   st ...header...\n"
            "   0: 00000000:0035 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000  0 22222 1 x\n"
            "   1: 0100007F:B3A1 08080808:0035 01 00000000:00000000 00:00000000 00000000  1000  0 22223 1 x\n";
        auto rows = parseProcNet(ProcNetKind::Udp4, udp4);
        CHECK(rows.size() == 2);
        inferDirections(rows);

        // unconnected udp on :53 -> kept, direction listen, no remote.
        const SocketRow* unbound = nullptr;
        for (const auto& r : rows) if (r.remote.addr.empty()) unbound = &r;
        CHECK(unbound != nullptr);
        CHECK(unbound && unbound->local.port == 53);
        CHECK(unbound && unbound->direction == "listen");
        CHECK(unbound && unbound->transport == "udp");

        // connected udp to 8.8.8.8:53 -> outbound.
        const SocketRow* dns = findByRemote(rows, "8.8.8.8", 53);
        CHECK(dns != nullptr);
        CHECK(dns && dns->direction == "outbound");
    }

    // --- a non-listen tcp row with no peer is dropped (not useful) --------
    {
        const std::string tcp4 =
            "header\n"
            "   0: 0100007F:CF5E 00000000:0000 08 x x x 0 0 9 1 x\n";  // CLOSE_WAIT, no peer
        auto rows = parseProcNet(ProcNetKind::Tcp4, tcp4);
        CHECK(rows.empty());
    }

    if (failures == 0) std::printf("parse_test: OK\n");
    else std::printf("parse_test: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
