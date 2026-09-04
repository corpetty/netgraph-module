#include "proc_net_parse.h"

#include <arpa/inet.h>  // inet_ntop, in_addr, in6_addr — present on Linux and macOS

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_set>

namespace netgraph {

namespace {

// Parse a run of hex digits into a uint64 (up to 16 nibbles). Returns false on a
// non-hex character, so a malformed line is dropped rather than misread.
bool hexToU64(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 16) return false;
    uint64_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= uint64_t(c - '0');
        else if (c >= 'a' && c <= 'f') v |= uint64_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= uint64_t(c - 'A' + 10);
        else return false;
    }
    out = v;
    return true;
}

// One byte from two hex chars at position i. Caller guarantees length.
bool hexByte(const std::string& s, size_t i, uint8_t& out) {
    uint64_t v;
    if (!hexToU64(s.substr(i, 2), v)) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

std::string formatV4(const std::string& hex8) {
    // /proc prints the v4 address as a 32-bit word in host (little-endian) byte
    // order, so the token "0100007F" is the byte sequence 01 00 00 7F whose LE
    // value is 0x7F000001 = 127.0.0.1. inet_ntop reads NETWORK order, so reverse
    // the four parsed bytes (7F 00 00 01) before feeding in_addr — endian-safe.
    if (hex8.size() != 8) return {};
    uint8_t b[4];
    for (size_t i = 0; i < 4; ++i)
        if (!hexByte(hex8, i * 2, b[i])) return {};
    uint8_t net[4] = { b[3], b[2], b[1], b[0] };
    struct in_addr a;
    std::memcpy(&a.s_addr, net, 4);
    char buf[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &a, buf, sizeof(buf))) return {};
    return buf;
}

std::string formatV6(const std::string& hex32) {
    // /proc stores the v6 address as four 32-bit words, each in host
    // (little-endian) byte order. Reverse the 4 bytes within each 8-hex group,
    // concatenate the groups in order, and inet_ntop the 16 bytes.
    if (hex32.size() != 32) return {};
    uint8_t bytes[16];
    for (size_t w = 0; w < 4; ++w) {
        uint8_t word[4];
        for (size_t i = 0; i < 4; ++i)
            if (!hexByte(hex32, w * 8 + i * 2, word[i])) return {};
        // reverse this word into the output
        for (size_t i = 0; i < 4; ++i) bytes[w * 4 + i] = word[3 - i];
    }
    struct in6_addr a6;
    std::memcpy(&a6.s6_addr, bytes, 16);
    char buf[INET6_ADDRSTRLEN];
    if (!inet_ntop(AF_INET6, &a6, buf, sizeof(buf))) return {};
    return buf;
}

bool isV6(ProcNetKind k) { return k == ProcNetKind::Tcp6 || k == ProcNetKind::Udp6; }
bool isUdpKind(ProcNetKind k) { return k == ProcNetKind::Udp4 || k == ProcNetKind::Udp6; }

}  // namespace

Endpoint parseHexEndpoint(ProcNetKind kind, const std::string& token) {
    Endpoint ep;
    const auto colon = token.find(':');
    if (colon == std::string::npos) return ep;
    const std::string addrHex = token.substr(0, colon);
    const std::string portHex = token.substr(colon + 1);

    uint64_t port = 0;
    if (hexToU64(portHex, port)) ep.port = static_cast<uint16_t>(port);

    ep.addr = isV6(kind) ? formatV6(addrHex) : formatV4(addrHex);
    return ep;
}

std::string normalizeState(const std::string& hexState, bool isUdp) {
    if (isUdp) {
        // UDP entries carry a state field but it isn't a connection state; the
        // remote endpoint (present vs 0.0.0.0:0) is what distinguishes connected
        // from unconnected. Report "established" and let the row's remote decide.
        return "established";
    }
    uint64_t s = 0;
    if (!hexToU64(hexState, s)) return "unknown";
    switch (s) {
        case 0x01: return "established";
        case 0x02: return "syn_sent";
        case 0x03: return "syn_recv";
        case 0x04: return "closing";      // FIN_WAIT1
        case 0x05: return "closing";      // FIN_WAIT2
        case 0x06: return "closing";      // TIME_WAIT
        case 0x07: return "closed";       // CLOSE
        case 0x08: return "closing";      // CLOSE_WAIT
        case 0x09: return "closing";      // LAST_ACK
        case 0x0A: return "listen";
        case 0x0B: return "closing";      // CLOSING
        case 0x0C: return "syn_recv";     // NEW_SYN_RECV
        default:   return "unknown";
    }
}

std::vector<SocketRow> parseProcNet(ProcNetKind kind, const std::string& contents) {
    std::vector<SocketRow> rows;
    const bool udp = isUdpKind(kind);
    const char* transport = udp ? "udp" : "tcp";

    std::istringstream in(contents);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) { first = false; continue; }  // header row: "sl local_address ..."
        if (line.empty()) continue;

        // Whitespace-tokenize. Columns:
        //  [0] sl:  [1] local  [2] rem  [3] st  [4] tx:rx  [5] tr:tm  [6] retr
        //  [7] uid  [8] timeout  [9] inode  ...
        std::vector<std::string> tok;
        std::istringstream ls(line);
        std::string t;
        while (ls >> t) tok.push_back(t);
        if (tok.size() < 10) continue;

        SocketRow row;
        row.transport = transport;
        row.local = parseHexEndpoint(kind, tok[1]);
        row.remote = parseHexEndpoint(kind, tok[2]);
        row.state = normalizeState(tok[3], udp);
        // inode is decimal, not hex.
        try { row.inode = std::stoull(tok[9]); } catch (...) { row.inode = 0; }

        // A remote of 0.0.0.0:0 / [::]:0 means no peer (listen or unconnected UDP).
        const bool noRemote = row.remote.port == 0 &&
                              (row.remote.addr.empty() || row.remote.addr == "0.0.0.0" ||
                               row.remote.addr == "::");
        if (noRemote) {
            row.remote = Endpoint{};  // normalize to empty
            if (!udp && row.state != "listen") {
                // A TCP row with no peer that isn't LISTEN is not useful; skip.
                continue;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void inferDirections(std::vector<SocketRow>& rows) {
    // Collect the local ports that are listening (TCP LISTEN, or UDP with no
    // remote and a bound port). A connected row on one of those local ports is
    // inbound; every other connected row is outbound.
    std::unordered_set<uint16_t> listenPorts;
    for (const auto& r : rows) {
        const bool udpUnbound = r.transport == "udp" && r.remote.addr.empty();
        if (r.state == "listen" || udpUnbound) {
            if (r.local.port != 0) listenPorts.insert(r.local.port);
        }
    }
    for (auto& r : rows) {
        if (r.state == "listen") { r.direction = "listen"; continue; }
        if (r.transport == "udp" && r.remote.addr.empty()) { r.direction = "listen"; continue; }
        if (r.remote.addr.empty()) { r.direction = "listen"; continue; }
        r.direction = listenPorts.count(r.local.port) ? "inbound" : "outbound";
    }
}

}  // namespace netgraph
