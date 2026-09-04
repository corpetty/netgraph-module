#!/usr/bin/env bash
# Build + run the pure netgraph tests without the Logos SDK. Provides a local
# logos_json.h shim over nlohmann/json. See tests/README.md.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
mod="$(dirname "$here")"
src="$mod/src"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# nlohmann/json.hpp include dir. Override with NLOHMANN_INC=/path (that contains
# nlohmann/json.hpp). Defaults to a vendored third_party/ next to this script.
NLOHMANN_INC="${NLOHMANN_INC:-$mod/third_party}"
if [ ! -f "$NLOHMANN_INC/nlohmann/json.hpp" ]; then
  echo "nlohmann/json.hpp not found under '$NLOHMANN_INC'."
  echo "Set NLOHMANN_INC to a dir containing nlohmann/json.hpp." >&2
  exit 2
fi

# Local logos_json.h shim (the SDK provides this in-tree).
cat > "$work/logos_json.h" <<'EOF'
#pragma once
#include <nlohmann/json.hpp>
using LogosMap  = nlohmann::json;
using LogosList = nlohmann::json;
EOF

INC=(-I"$src" -I"$work" -I"$NLOHMANN_INC")
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -Wall -Wextra -pthread)
rc=0

build_run() { # name, sources...
  local name="$1"; shift
  echo "== $name =="
  "$CXX" "${FLAGS[@]}" "${INC[@]}" "$@" -o "$work/$name"
  "$work/$name" || rc=1
}

build_run parse_test     "$here/parse_test.cpp"     "$src/proc_net_parse.cpp"
build_run merge_test     "$here/merge_test.cpp"     "$src/merge.cpp"
build_run sweep_test      "$here/sweep_test.cpp"     "$src/sweep.cpp" "$src/merge.cpp" "$src/fake_sources.cpp"
if [ "$(uname -s)" = "Linux" ]; then
  build_run linux_live_test "$here/linux_live_test.cpp" \
    "$src/linux_socket_table.cpp" "$src/linux_process_source.cpp" "$src/proc_net_parse.cpp"
fi

echo
[ "$rc" -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$rc"
