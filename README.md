# netgraph

A [Logos Basecamp](https://logos.co) module that shows, in real time, every
network connection the running Logos process tree holds, grouped by the service
that opened it and the network or overlay it belongs to.

Scope is the **whole process tree**, not only Logos overlays — DNS lookups,
plain HTTPS to a bootstrap host, and RPC endpoints all appear. Nothing is
hidden, including the connections a sovereignty-minded user would not expect to
see. It is **local only** (no export, no telemetry) and collection is **off
until turned on** behind an explicit user switch.

Two deliverables:

1. `netgraph_module` — the backend that collects and merges connection data (this repo, `module/`).
2. `netgraph_ui` — a QML plugin that renders the graph (later, `ui/`).

See [`DESIGN.md`](DESIGN.md) for the architecture, the prior-art patterns copied
from [`openmetrics-module`](https://github.com/logos-co/openmetrics-module), the
process-tree attribution finding, and milestone status.

## Architecture (short)

Two collectors feeding one merge step:

- **Collector A — socket table (primary).** Enumerates open sockets per pid
  across the Logos process tree. Works with no cooperation from any module, so
  it produces a real graph on day one and covers DNS and HTTPS. Platform-split
  behind one interface: Linux `/proc/net` + `/proc/<pid>/fd`, macOS `libproc`,
  plus a fake for tests. No shelling out to `lsof`/`ss`/`netstat`.
- **Collector B — module labels (enrichment).** Binds the `connection_source`
  interface (`collectConnections()`) to operator-chosen modules and turns
  `1.2.3.4:9000` into "mix node, peer id X". Labels only — never creates a row.
- **Merge.** Keys on `(pid, local port, remote endpoint)`. A socket row is the
  authority for existence; a module row with no matching socket is kept as a
  derived edge and marked. `module`/`network`/`peer_id` are nullable by design —
  an unlabelled row is still shown.

Process discovery is name-independent (process ancestry), never by matching
process names.

## Module API

| Method | Signature | Purpose |
|---|---|---|
| `setEnabled` | `setEnabled(config: string) -> int64` | The explicit switch. `{enabled, sweep_ms, sources, include_host, root_pid}`. Off retains nothing. |
| `snapshot` | `snapshot() -> string` | The last completed sweep's merged connection array (the UI polls this). |
| `getInfo` | `getInfo() -> string` | `{enabled, sweeping, sweep_ms, include_host, sockets, attributed, derived, sources}`. |

## Build

```bash
cd module
nix build            # the plugin
nix build .#lgx      # a .lgx package
```

Install with `lgpm install --file` or through Basecamp's Package Manager.

## Tests

Pure unit tests (parse, merge, sweep) run without the Logos SDK, plus a live
Linux collector check:

```bash
cd module && NLOHMANN_INC=/path/to/nlohmann-include ./tests/run_local.sh
```

See [`module/tests/README.md`](module/tests/README.md).

## Status

M0 (Collector A + the pure sweep pipeline) is built and tested; the Linux
collector is verified against a live `/proc`. Collector B (provider bind + pid
name attribution) and the `logoscore` doctest are next. See `DESIGN.md`.
