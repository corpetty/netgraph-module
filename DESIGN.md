# netgraph — skeleton proposal

Status: skeleton and interface, before the collector. This is the handoff's
first step — read `openmetrics-module` in full, then propose the `netgraph_module`
skeleton and the `connection_source` header. No collector, no classifier, no UI.

What was read: `logos-co/openmetrics-module` at `5dbca24` — README, `metadata.json`,
`interfaces/metrics_source.h`, `src/openmetrics_impl.{h,cpp}`, `src/openmetrics_format.*`,
`CMakeLists.txt`, `flake.nix`, and `doctests/openmetrics.test.yaml`. Also the two-part
(core + ui) packaging in `corpetty/muster` (`module/` and `ui/` `metadata.json`).

## Files in this proposal

```
netgraph/
  DESIGN.md                         this document
  module/                           netgraph_module (the observer)
    metadata.json                   interface: universal; interface_dependencies: connection_source
    CMakeLists.txt                  logos_module(NAME netgraph ...)
    flake.nix                       mkLogosModule, builder pinned 0.2.5
    interfaces/
      connection_source.h           the collectConnections() contract (M3), proposed now
    src/
      netgraph_impl.h               module surface: setEnabled / snapshot / getInfo
      netgraph_impl.cpp             surface + state; sweepOnce() is a marked stub
      collector.h                   the platform-split socket-table seam (ISocketTable)
```

Not here yet, by design: the socket-table bodies (`linux_socket_table.cpp`,
`macos_socket_table.cpp`, `fake_socket_table.cpp`), the classifier (M1), the QML
plugin `netgraph_ui` (M2), the CDDL schema and reference provider (M3).

## What is copied from openmetrics, and the netgraph mapping

| openmetrics | netgraph | why |
|---|---|---|
| `interface_dependencies: metrics_source`, bound at runtime to operator-named modules | `interface_dependencies: connection_source`, same binding | ships before any provider exists; Collector B degrades to nothing |
| convention method `collectMetrics()` | convention method `collectConnections()` | a module opts in with one method; missing/broken module is skipped |
| pure-C++ `universal` module, no Qt; `logos_sdk.h` only in the `.cpp` | same | the generator parses the impl header and expects plain C++ |
| worker thread → IPC marshaled to the owner thread by the SDK | timer (sweep) thread → same marshaling | our collector runs off the event loop; the handoff calls this out |
| `scrape()` direct-call method the UI/debug reads | `snapshot()` direct-call method the UI polls | the UI never needs a socket of its own |
| one bad module never breaks a scrape | one bad provider never breaks a sweep | same guarantee |
| literate doctest under `logoscore`: build providers inline, run, assert | M0 doctest: open a known connection, see it in `snapshot()` | end-to-end proof on the commit under test |

What is deliberately NOT copied: connections do not go into `collectMetrics()`.
Per-peer labels blow up Prometheus cardinality — that is the reason this is a
separate interface, not a metrics family. openmetrics is consumed instead as an
optional cross-check (below).

## Module surface, and where it differs from openmetrics

Three methods, all pure C++, all on `LogosModuleContext`:

- `setEnabled(configJson) -> int64` — the explicit user switch. openmetrics has
  no equivalent; netgraph needs one because it sees the host's complete
  connection graph, a surveillance surface inside a sovereignty app. Collection
  is off until turned on, and retains nothing while off. Config carries
  `sweep_ms`, the `sources` list (connection_source providers), and
  `include_host`.
- `snapshot() -> string` — the merged connection array, the last completed
  sweep, served from cache under the lock. Never blocks on a live sweep. This is
  the `scrape()` analogue.
- `getInfo() -> string` — `{enabled, sweeping, sweep_ms, sockets, attributed,
  derived, sources}`.

openmetrics' `start`/`stop` map onto `setEnabled` here: there is no HTTP server
to stand up, only a sweep timer to run, so one switch covers it.

## The two collectors and the merge (restated for the skeleton)

- Collector A, socket table (`collector.h`) — authority for a connection's
  existence. Platform-split behind `ISocketTable`, two real impls + a fake. Pids
  come from liblogos process stats, never from process-name matching. No
  `lsof`/`ss`/`netstat`.
- Collector B, module labels — the bound `connection_source` providers. Labels
  only; never creates a row.
- Merge key `(pid, local port, remote endpoint)`. A module row with no matching
  socket is kept as a derived edge and marked, not merged into a socket row.

Every row is shown even when `module`/`network`/`peer_id` are null — the
unlabelled rows are the point.

## Open questions — recommendations

1. **Sweep interval; push vs poll.** Module sweeps on its own timer and caches
   the latest snapshot; the UI polls `snapshot()` at ~1 Hz. This keeps the
   module free of UI coupling and mirrors openmetrics (the reader pulls a cached
   document). Default `sweep_ms` 1000, floor 250. Deltas are a later
   optimization, not M0. Rationale: the constraint "never block the module event
   loop on a sweep" is satisfied by a timer thread + a cached publish, and a poll
   needs no subscription machinery to land M0.

2. **Include the Basecamp host and `ui-host` sockets?** Include, marked as host
   (`include_host: true` default; rows tagged so the UI can filter). Consistent
   with "nothing is hidden." Making it a config flag lets an operator narrow to
   module subprocesses when the host noise gets in the way.

3. **Retention.** Snapshot-only through M0–M2. The M2 churn handling (hold a
   node a few seconds after its last socket closes, animate the decay) lives in
   the UI, not as module history. A bounded, opt-in rolling ring buffer for a
   timeline view is a later add and stays within the local-only constraint. Start
   with no history so there is less state to get wrong and less to retain on a
   surveillance surface.

## Handoff points to confirm before the collector

1. **Unconnected UDP breaks the merge key.** `/proc/net/udp` (and libproc) report
   no remote for a socket that never called `connect()` — some DNS resolvers work
   this way. Such a row has `(pid, local port, —)` and cannot key on a remote
   endpoint. Proposal: show it as a socket with a null remote (still a row), and
   never let it match a Collector B label by remote. QUIC over a connected UDP
   socket is fine. Confirm this is the intended handling.

2. **`id` is ephemeral, not persistent.** "stable hash of pid + local port +
   remote endpoint" is stable within a run, but pids recycle and ports are reused
   across runs, so the same `id` can name a different connection after a restart.
   Fine for a live view; flagging so nothing downstream treats `id` as durable.

3. **openmetrics cross-check is an optional, declared dependency.** Comparing a
   module's reported peer count against its socket count needs openmetrics
   running and configured. Under `--access-policy enforce` that is a concrete
   dependency netgraph must declare; it should be optional (absent openmetrics =
   no cross-check, not a failure). Confirm whether the cross-check is in scope for
   an early milestone or deferred.

4. **Access policy entries.** `netgraph_module` binds `connection_source`
   providers (declared via `interface_dependencies`, so allowed under enforce).
   `netgraph_ui -> netgraph_module` needs an explicit policy entry because
   `ui_qml` plugins are not tracked as dependents. Test under `enforce` from M0.

## Packaging / integration

This directory is a staging scaffold. The catalog builds each module from a git
submodule under `submodules/` (`release-all.yml` discovers `path = submodules/*`
in `.gitmodules`), so netgraph enters the pipeline by living in its own repo
(e.g. `corpetty/netgraph`, `module/` + later `ui/`, mirroring muster) and being
added with `scripts/add-module.sh`. A plain directory at the catalog root is not
picked up by CI — intentional while this is a pre-collector skeleton.

Build once extracted: `nix build .#lgx`; install with
`lgpm install --file` or Basecamp's Package Manager. Develop against a second
instance: `LogosBasecamp --user-dir /tmp/basecamp-ng`.

## Process-tree attribution — a handoff assumption that does not hold

Reading the SDK/core to wire M0 turned up a blocker. The handoff says the pid
list "comes from the per-module process stats liblogos already reports
(`logoscore stats` — name, pid, cpu, memory)". That table is real, but it is a
**host-only** API: `logos_core_get_module_stats()` in liblogos, reached through
`logos::host::LogosCore` (`logos_host_core.h`), constructed once in a host's
`main()`. It is **not reachable from a universal Basecamp module**:

- `LogosModuleContext::modules()` exposes only the typed wrappers for the
  module's own declared dependencies — no pid list, no process table.
- Basecamp reads the stats in `CoreModuleManager` (a 2 s timer) and surfaces
  them to QML; it never re-exports them to modules.
- The `logoscore-cli` daemon *does* register a `core_service` module with a
  `getModuleStats()` method — but it only exists under that daemon, is gated by
  a CLI token, and is absent under Basecamp.

So the pid problem splits in two, and the split is now in the code:

1. **Existence — which pids to sweep.** Solved module-side, no host help, by
   process **ancestry**: the descendants of the Logos host process
   (`process_source.h` → `linux_process_source.cpp` / `macos_process_source.cpp`,
   walking `/proc/<pid>/stat` ppid or libproc `pbsi_ppid`). This is name-independent,
   so it honours the handoff's one prohibition ("do not discover the tree by
   matching process names"). Collector A runs fully on this — the existence
   graph, DNS and HTTPS included, works with zero host cooperation. **This is
   M0's headline and it is built and tested.**

2. **Name attribution — pid → module name.** Needs the host stats, and so does
   Collector B (to place a provider's rows by its pid). Left behind a resolver
   that fills `ProcInfo.name` / `MergeContext.pidNames`; absent it, every row
   still carries a real pid and `module: null` — shown, per "the unlabelled rows
   are the point". Three ways to supply it, **a decision for you** because it
   sets the module's dependency shape:

   - **(a) core_service under the logoscore-cli daemon.** Zero new code upstream;
     `core_service.getModuleStats()` gives name (+pid, see the finding below).
     But it is daemon-only — it does **not** attribute anything under Basecamp,
     where the module ships. Good enough to make the **M0 doctest** show real
     names (the doctest runs under `logoscore`), not a production answer.
   - **(b) a small stats-exporting core module**, declared as a netgraph
     dependency, that wraps `logos_core_get_module_stats()` and exposes it over
     an inter-module call. Works under both hosts and stays within the
     interface-dependency model. Costs one new upstream module.
   - **(c) host-fed.** Basecamp passes the stats into the module (e.g. via config
     on the user switch, refreshed). No new module, but couples to Basecamp and
     needs a host change.

   Recommendation: **(a) for the M0 doctest now** (it needs nothing new and
   proves attribution end-to-end where the doctest runs), and **(b) as the
   production path** for Basecamp (smallest change that attributes under the real
   host and fits the interface-dependency model). (c) only if a host change is
   already on the table.

   **DECISION (2026-09-04): (a) now for the M0 doctest, (b) for production.** The
   resolver seam (`name_resolver.h`, `INameResolver`) and the pure stats decode
   (`parseModuleStats`) are built and tested to this shape; `netgraph_impl`
   defaults to `NullNameResolver` and installs the real resolver via
   `makeResolver()` per the chosen path.

   **FINDING (2026-09-04) — `getModuleStats()` omits `pid`.** Verified against
   `logos-liblogos/src/logos_core/process_stats.cpp`: the real payload is
   `[{name, cpu_percent, cpu_time_seconds, memory_mb}]` — **no `pid`**. Both (a)
   and (b) join a socket (which carries a pid) to a name, so both need the pid.
   The pid is already computed in that function (`process->processId()`); emitting
   it is a **one-line upstream add** (`moduleObj["pid"] = (double)pid;`).
   `parseModuleStats` reads `pid` when present and degrades to name-only
   (`hasPid=false` → no attribution, `module:null`) when absent — so netgraph is
   correct either way and the one-liner simply switches attribution on.

   **UPDATE (2026-09-08) — the stats source moved; the finding still holds.**
   `process_stats` has since been extracted from liblogos into its own
   `process-stats` library (liblogos now imports it via `PROCESS_STATS_ROOT` and
   calls `ProcessStats::getModuleStats(ModuleManager::getModuleProcessIds())`).
   `getModuleStats` now takes a name→pid map as input, but its JSON output is
   still `[{name, cpu_percent, cpu_time_seconds, memory_mb}]` — **no `pid`**
   (verified against the built `process-stats` source). So the one-line add now
   lands in the `process-stats` repo's `getModuleStats` (`moduleObj["pid"] =
   pid;` — the pid is already in scope as the map's value), not in liblogos.
   `parseModuleStats` is unchanged and still handles both payload shapes.

## M0 status

**NAMING FIX (2026-09-08).** `metadata.json` and `CMakeLists.txt` named the
module `netgraph` / `netgraph_plugin`, but every sibling core module registers
as `<x>_module` (`storage_module`, `delivery_module`, `accounts_module`, …) and
this design, the README, and the access-policy notes all call it
`netgraph_module`. Aligned: `logos_module(NAME netgraph_module)`, metadata
`name: "netgraph_module"`, `main: "netgraph_module_plugin"` (the builder's
`OUTPUT_NAME` is `${NAME}_plugin`; the CMake target is `${NAME}_module_plugin` =
`netgraph_module_module_plugin`, referenced by the `Threads` link line). The
module now loads as `netgraph_module`, matching the M0 doctest and the policy
entry `netgraph_ui -> netgraph_module`.

Built and unit-tested (pure, no SDK — `tests/run_local.sh`, all green), and the
Linux collector verified against this host's live `/proc`:

- `proc_net_parse.{h,cpp}` — `/proc/net/{tcp,tcp6,udp,udp6}` decode (v4/v6
  endianness, state map, direction inference). Tested.
- `merge.{h,cpp}` — the A/B merge, pid→name attribution, derived edges, host
  tagging, stable id. Tested.
- `sweep.{h,cpp}` — the pure pipeline `buildSnapshot()` (discover → enumerate →
  merge → document). Tested over the fakes.
- `collector.h` + `linux_socket_table.cpp` — Collector A on Linux, `/proc/net`
  parse + inode→pid via `/proc/<pid>/fd`. Verified live.
- `process_source.h` + `linux_process_source.cpp` — ancestry pid discovery.
  Verified live.
- `socket_table_factory.cpp` + `fake_sources.cpp` — platform pick + fakes.
- `netgraph_impl.{h,cpp}` — the `setEnabled`/`snapshot`/`getInfo` surface and the
  timer thread driving `buildSnapshot`. Compiles against the SDK (not buildable
  in the dev sandbox); logic factored into the tested pure functions.
- `macos_socket_table.cpp` / `macos_process_source.cpp` — libproc impls, written
  from the documented API, **unverified on this host** (Linux sandbox); verify on
  Apple Silicon.

## Collector B + resolver status (2026-09-04)

The attribution decision is made, so Collector B and the resolver are wired at
every layer that does not require the live SDK/host, and unit-tested:

- `provider_parse.{h,cpp}` — PURE Collector B: a `connection_source` payload →
  `ProviderLabel`s for a given `(module, pid)`. Strict on the match key (no usable
  remote endpoint → entry dropped), lenient on labels (unknown transport/direction
  ignored), never throws on an untrusted feed (per-entry skip + count). Tested
  (`tests/provider_parse_test.cpp`).
- `name_resolver.{h,cpp}` — PURE attribution decode + the `INameResolver` seam +
  `NullNameResolver`. `parseModuleStats` handles the real name-only payload and
  the pid-augmented one; `pidNamesFrom` / `pidForName` are the joins. Tested
  (`tests/name_resolver_test.cpp`).
- `sweep.buildSnapshot` — now takes resolver-supplied `extraNames` (pid→name),
  overlaid on the ancestry source's names. Tested (`tests/sweep_test.cpp`).
- `netgraph_impl.cpp` — the SDK shell: one `m_resolver->stats()` read per sweep
  feeds both Collector A attribution (`extraNames`) and Collector B pid placement;
  `collectProviderLabels` binds each source via
  `modules().bind_connection_source(name).collectConnections()` and parses it
  through `provider_parse`. One broken/missing provider is skipped, never fatal.
  Compiles against the SDK (not buildable in the dev sandbox); all logic sits in
  the tested pure functions.

Deferred:

- ~~**Install the real resolver** for path (a).~~ **Done (2026-09-08).** The
  exact `core_service` API was confirmed against `logos-logoscore-cli`
  (`src/core_service/core_service_impl.h`): `LogosList getModuleStats()`, whose
  body is `nlohmann::json::parse(logos_core_get_module_stats())` — i.e. the
  process-stats payload verbatim. So netgraph now declares a second
  `interface_dependency` on `core_service` (`interfaces/core_service.h`,
  `impl_class: ICoreService`), and `makeResolver()` (now a member, so it can
  reach `modules()`) returns a `CallbackNameResolver` whose fetch is
  `modules().bind_core_service("core_service").getModuleStats()`, decoded by the
  pure `parseModuleStats`. `CallbackNameResolver` swallows a failing fetch to an
  empty result, so on any host without `core_service` (Basecamp) it degrades to
  exactly the old `NullNameResolver` behaviour — module:null on every row — which
  makes installing path (a) unconditionally safe (strict superset). Path (b) is
  then a drop-in: the same `ICoreService` interface bound to a different module
  name, no code change here. Still gated on the process-stats `pid` add (PR
  `logos-co/process-stats#4`) to actually attribute; correct either way until
  then. Not buildable in the dev sandbox — validated by the tested pure
  `CallbackNameResolver` unit test + CI's SDK build.
- **M0 doctest** under `logoscore` — **written (2026-09-08)**, at
  `module/doctests/netgraph-module-m0.test.yaml` (+ `run.sh`), mirroring the
  in-repo `*-module-runtime.test.yaml` specs the sibling modules ship
  (storage/delivery — the openmetrics analogue). It packages this commit as an
  `.lgx`, installs it with `lgpm`, loads it into a headless daemon, opens a
  **known** loopback connection held by a throw-away process, enables collection
  scoped to that process (`root_pid`), and asserts `snapshot()` contains the
  endpoint (`127.0.0.1` / `54545`). It asserts connection **existence**, not
  module names — name attribution still waits on the path-(a) resolver (below).
  The socket-visibility core (fixture → `buildSnapshot` scoped to its pid →
  endpoint in the document) is verified against this host's live `/proc`; the
  full run needs the published repo + nix stack, so it is CI-ready but not
  runnable in the dev sandbox.
- **openmetrics cross-check** — reads openmetrics' aggregated counters, compares
  reported peers vs sockets per pid, surfaces the gap. Optional declared
  dependency; lands with the production resolver.
- **macOS** — `macos_socket_table.cpp` / `macos_process_source.cpp` still
  unverified off-device; verify on Apple Silicon.

## Next step

1. ~~Land the one-line `pid` add in the `process-stats` repo's
   `getModuleStats()`.~~ **PR open: `logos-co/process-stats#4`** (needs a
   maintainer merge). Until it lands, attribution stays off and every row is
   `module:null` — correct, just unlabelled.
2. ~~Install the path-(a) `core_service` resolver in `makeResolver()` and confirm
   the exact bind API.~~ **Done (2026-09-08)** — signature confirmed against
   `logos-logoscore-cli` and wired via a `core_service` interface_dependency +
   `CallbackNameResolver`; see the Collector B "Deferred" note above.
3. ~~Write the M0 doctest.~~ **Done (2026-09-08)** —
   `module/doctests/netgraph-module-m0.test.yaml` on `main`; CI to run it
   (`.github/workflows/{ci,doctests}.yml`) is in a companion PR. Note: the
   doctest asserts connection **existence**, not names — its fixture is a plain
   process, not a loaded module, so `core_service` never attributes it. Asserting
   names needs a **module-owned** connection in the fixture and process-stats#4
   merged; that is a later doctest, not a tweak to this one.
4. **First green CI run** — the CI workflows are the first build of the
   SDK-facing code (`netgraph_impl` + generated `bind_connection_source` /
   `bind_core_service`) and the first live daemon run of the doctest; neither was
   possible in the dev sandbox. Watch that run and fix whatever the real SDK/host
   surfaces (e.g. the exact generated bind-wrapper spelling).
5. macOS verification on an Apple Silicon box in parallel.
