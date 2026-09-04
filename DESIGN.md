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
     `core_service.getModuleStats()` gives name+pid. But it is daemon-only — it
     does **not** attribute anything under Basecamp, where the module ships. Good
     enough to make the **M0 doctest** show real names (the doctest runs under
     `logoscore`), not a production answer.
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

## M0 status

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

Deferred:

- **Collector B** provider bind + the pid→name resolver — both wait on the
  attribution decision above (marked TODO in `netgraph_impl.cpp`).
- **M0 doctest** under `logoscore` — open a known connection, see it in
  `snapshot()` (mirrors openmetrics' `doctests/`). Wants the attribution path
  chosen so the doctest can assert real module names via (a).
- openmetrics cross-check (you scoped it in early) — reads openmetrics'
  aggregated counters, compares reported peers vs sockets per pid, surfaces the
  gap. An optional declared dependency; lands with Collector B.

## Next step

Decide the attribution path (a/b/c), then wire Collector B + the resolver and
write the M0 doctest. macOS verification on an Apple Silicon box in parallel.
