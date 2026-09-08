#include "provider_parse.h"

namespace netgraph {

namespace {

// Guarded readers — a provider feed is untrusted, so a wrong JSON type must never
// throw out of the parse. nlohmann's value() throws on a type mismatch; these
// return the default instead.
std::string strOr(const LogosMap& o, const char* key) {
    auto it = o.find(key);
    if (it != o.end() && it->is_string()) return it->get<std::string>();
    return std::string();
}

bool boolOr(const LogosMap& o, const char* key, bool dflt) {
    auto it = o.find(key);
    if (it != o.end() && it->is_boolean()) return it->get<bool>();
    return dflt;
}

// Read a port in [0,65535]. Returns false if the key is absent or not a valid
// integer port. Non-integer JSON numbers (e.g. 80.5) are rejected.
bool portOr(const LogosMap& o, const char* key, uint16_t& out) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_number_integer()) return false;
    long long v = it->get<long long>();
    if (v < 0 || v > 65535) return false;
    out = static_cast<uint16_t>(v);
    return true;
}

}  // namespace

ProviderParseResult parseConnectionSourcePayload(const std::string& module,
                                                 int64_t pid,
                                                 const LogosMap& payload) {
    ProviderParseResult r;

    auto conns = payload.is_object() ? payload.find("connections") : payload.end();
    if (conns == payload.end() || !conns->is_array()) {
        r.validShape = false;
        return r;
    }

    for (const auto& e : *conns) {
        try {
            if (!e.is_object()) { ++r.skipped; continue; }

            // Match key: a usable remote endpoint. remote.port is required; addr
            // may be empty (some OS rows report none), matching SocketRow.
            auto rem = e.find("remote");
            if (rem == e.end() || !rem->is_object()) { ++r.skipped; continue; }
            uint16_t rport = 0;
            if (!portOr(*rem, "port", rport)) { ++r.skipped; continue; }

            ProviderLabel lbl;
            lbl.module      = module;
            lbl.pid         = pid;
            lbl.remote.addr = strOr(*rem, "addr");
            lbl.remote.port = rport;
            lbl.host        = strOr(*rem, "host");

            // local.port is optional; its presence widens the merge key from
            // (pid,remote) to (pid,localPort,remote).
            auto loc = e.find("local");
            if (loc != e.end() && loc->is_object()) {
                uint16_t lport = 0;
                if (portOr(*loc, "port", lport)) {
                    lbl.hasLocalPort = true;
                    lbl.localPort = lport;
                }
            }

            lbl.peerId  = strOr(e, "peer_id");
            lbl.network = strOr(e, "network");

            // transport is a hint only (the socket table wins for tcp/udp); accept
            // just the known refinements, ignore anything else rather than reject.
            std::string t = strOr(e, "transport");
            if (t == "tcp" || t == "udp" || t == "quic") lbl.transport = t;

            std::string d = strOr(e, "direction");
            if (d == "inbound" || d == "outbound") lbl.direction = d;

            lbl.derived = boolOr(e, "derived", false);

            r.labels.push_back(std::move(lbl));
        } catch (...) {
            ++r.skipped;  // backstop: no single entry can break the sweep
        }
    }

    return r;
}

}  // namespace netgraph
