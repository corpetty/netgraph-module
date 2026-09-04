# netgraph module tests

Pure, framework-free unit tests for the parts of Collector A + the merge that
don't need the Logos SDK. Each file has its own `main()` and returns non-zero on
failure.

| Test | Proves | Needs |
|---|---|---|
| `parse_test.cpp` | `/proc/net` decode: v4/v6 address+port endianness, TCP/UDP state mapping, listen/inbound/outbound inference, no-remote handling | nothing (stdlib) |
| `merge_test.cpp` | A/B merge: socket rows authoritative, provider labels enrich, pid→name attribution, derived edges marked, host tagging, null labels shown, stable id | a `LogosMap` (nlohmann::json) |
| `sweep_test.cpp` | the whole pure pipeline (`buildSnapshot`) over the fakes: discovery → enumerate → merge → document, include-host on/off, Collector A alone | `LogosMap` + fakes |
| `linux_live_test.cpp` | Collector A end-to-end on a live Linux host: open a listener, find it via `/proc` + inode→pid, ancestry source includes self | Linux `/proc`, `-pthread` |

## Run locally (no SDK)

`LogosMap` is the SDK's alias for `nlohmann::json`. Outside the builder, provide
a one-line shim `logos_json.h` on the include path:

```cpp
#pragma once
#include <nlohmann/json.hpp>
using LogosMap = nlohmann::json;
using LogosList = nlohmann::json;
```

Then:

```bash
./tests/run_local.sh            # builds + runs every test above
```

The script needs `g++` (C++17) and `nlohmann/json.hpp` reachable via an include
dir passed as `NLOHMANN_INC` (defaults to a vendored `third_party/`).

## Not covered here

- The `NetgraphImpl` timer + Collector B provider bind — needs the generated
  `logos_sdk.h`; built by the module proper.
- The macOS libproc socket table / process source — compiled and verified only
  on Apple Silicon (the files self-guard with `#if __APPLE__`).
- The M0 **doctest** — open a known connection under a `logoscore` daemon and
  see it in `snapshot()`. That is the milestone's acceptance proof and lands
  with the doctest harness (mirrors openmetrics' `doctests/`).
