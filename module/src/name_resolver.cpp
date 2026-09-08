#include "name_resolver.h"

namespace netgraph {

std::vector<ModuleStat> parseModuleStats(const LogosMap& payload) {
    std::vector<ModuleStat> out;

    const LogosMap* arr = nullptr;
    if (payload.is_array()) {
        arr = &payload;
    } else if (payload.is_object()) {
        auto it = payload.find("modules");
        if (it != payload.end() && it->is_array()) arr = &(*it);
    }
    if (!arr) return out;

    for (const auto& e : *arr) {
        if (!e.is_object()) continue;
        auto n = e.find("name");
        if (n == e.end() || !n->is_string()) continue;
        std::string name = n->get<std::string>();
        if (name.empty()) continue;

        ModuleStat m;
        m.name = std::move(name);
        auto p = e.find("pid");
        if (p != e.end() && p->is_number_integer()) {
            long long pid = p->get<long long>();
            if (pid > 0) { m.pid = pid; m.hasPid = true; }
        }
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<ModuleStat> parseModuleStats(const std::string& json) {
    LogosMap parsed;
    try {
        parsed = LogosMap::parse(json);
    } catch (...) {
        return {};
    }
    return parseModuleStats(parsed);
}

std::unordered_map<int64_t, std::string> pidNamesFrom(const std::vector<ModuleStat>& stats) {
    std::unordered_map<int64_t, std::string> m;
    for (const auto& s : stats)
        if (s.hasPid) m[s.pid] = s.name;
    return m;
}

int64_t pidForName(const std::vector<ModuleStat>& stats, const std::string& name) {
    for (const auto& s : stats)
        if (s.hasPid && s.name == name) return s.pid;
    return 0;
}

}  // namespace netgraph
