// Pure unit tests for pid<->name attribution parsing. Covers the REAL host
// getModuleStats shape (name + cpu/mem, no pid), the augmented shape (pid added
// upstream), the {"modules":[...]} wrapper, malformed JSON, and the pid joins.

#include <cassert>
#include <cstdio>
#include <string>

#include "name_resolver.h"

using namespace netgraph;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    // The shape the host emits TODAY (process_stats.cpp): no pid. Parses to names
    // with hasPid=false, so attribution degrades to nothing (module stays null).
    {
        auto s = parseModuleStats(std::string(R"([
          {"name":"blockchain_module","cpu_percent":1.2,"cpu_time_seconds":3.4,"memory_mb":50.0},
          {"name":"mix_module","cpu_percent":0.1,"cpu_time_seconds":0.2,"memory_mb":12.0}
        ])"));
        CHECK(s.size() == 2);
        CHECK(s[0].name == "blockchain_module");
        CHECK(!s[0].hasPid);
        CHECK(pidNamesFrom(s).empty());
        CHECK(pidForName(s, "blockchain_module") == 0);
    }

    // The augmented shape (one-line upstream add of "pid"): attribution works.
    {
        auto s = parseModuleStats(std::string(R"([
          {"name":"blockchain_module","pid":343773,"memory_mb":50.0},
          {"name":"mix_module","pid":343999},
          {"name":"","pid":1},
          {"name":"no_pid_module"},
          {"name":"bad_pid","pid":-5},
          "garbage",
          {"cpu_percent":9.9}
        ])"));
        // "": empty name skipped. "garbage": not an object. last: no name. => 4 kept.
        CHECK(s.size() == 4);

        auto names = pidNamesFrom(s);
        CHECK(names.size() == 2);                       // only the two with real pids
        CHECK(names[343773] == "blockchain_module");
        CHECK(names[343999] == "mix_module");
        CHECK(pidForName(s, "blockchain_module") == 343773);
        CHECK(pidForName(s, "no_pid_module") == 0);     // present but unattributed
        CHECK(pidForName(s, "bad_pid") == 0);           // negative pid rejected
        CHECK(pidForName(s, "absent") == 0);
    }

    // {"modules":[...]} wrapper tolerated.
    {
        auto s = parseModuleStats(std::string(R"({"modules":[{"name":"x","pid":7}]})"));
        CHECK(s.size() == 1);
        CHECK(pidForName(s, "x") == 7);
    }

    // Malformed / empty inputs -> empty, no throw.
    CHECK(parseModuleStats(std::string("not json")).empty());
    CHECK(parseModuleStats(std::string("{}")).empty());
    CHECK(parseModuleStats(std::string("[]")).empty());

    if (failures == 0) std::printf("name_resolver_test: OK\n");
    else std::printf("name_resolver_test: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
