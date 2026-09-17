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
2. `netgraph_ui` — a QML plugin that renders the graph (this repo, `ui/`).

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

The backend:

```bash
cd module
nix build              # the plugin
nix build .#lgx-portable   # a self-contained .lgx to install into Basecamp
```

The view (`netgraph_ui`, a `ui_qml` plugin):

```bash
cd ui
nix build              # the QML plugin (repc codegen + generated glue + QML)
nix build .#lgx-portable   # a self-contained .lgx to install into Basecamp
nix run .              # launch the view standalone (logos-standalone-app + ui-host)
```

Install each `result/*.lgx` with `lgpm install --file result/*.lgx` or through
Basecamp's Package Manager.

**Use `.#lgx-portable`, not `.#lgx`, for anything you install into Basecamp.**
`.#lgx` produces the *dev* variant (`linux-amd64-dev`): it resolves its libraries
from `/nix/store` at runtime, so `lgpm`/Basecamp reject it ("no variant matching
this platform"). It is only for a dev host running the module straight from the
store (e.g. `nix run .`). `.#lgx-portable` produces the release variant
(`linux-amd64`) with its dependencies bundled alongside the plugin — that is what
installs.

`ui/` pins `netgraph_module` by the committed `flake.lock` (a published github
ref), so its `.lgx` bundles the *published* backend. To co-develop against your
local backend tree, override the input (note: this reintroduces the dev-tree
variant, so use it for `nix build` / `nix run`, not for a Basecamp install):

```bash
cd ui && nix build --override-input netgraph_module path:../module
```

## Access policy

Under a host running `--access-policy` in `enforce` mode, `netgraph_ui` must be
listed as an allowed caller of `netgraph_module` — a `ui_qml` plugin is not
auto-tracked as a dependent, so the host won't add it on its own. The entry is
shipped as [`access-policy.example.json`](access-policy.example.json); see
[`ACCESS-POLICY.md`](ACCESS-POLICY.md) for the contract and how to merge it into
a deployment's policy.

## Tests

Pure unit tests (parse, merge, provider-parse, name-resolver, sweep) run without
the Logos SDK, plus a live Linux collector check:

```bash
cd module && NLOHMANN_INC=/path/to/nlohmann-include ./tests/run_local.sh
```

See [`module/tests/README.md`](module/tests/README.md).

## Status

M0 (Collector A + the pure sweep pipeline) is built and tested; the Linux
collector is verified against a live `/proc`. Collector B is now wired: the pure
payload parser, the pid↔name resolver seam, and the `netgraph_impl` binding are
in place and unit-tested; what remains is installing the real (path-a) resolver
under `logoscore` — including a one-line upstream `pid` add to `getModuleStats()`
— and the `logoscore` doctest.

M2 (`netgraph_ui`) is built and **confirmed running in Basecamp**: a universal
`ui_qml` plugin whose backend forwards to `netgraph_module` (typed `setEnabled` /
`snapshot` / `getInfo`) and whose QML view renders the merged connection document
as a live table, with the collection switch, sweep interval, and host-include
control wired to `setEnabled`. Turning collection on drives the sweeps and the
rows populate live. Unlabelled rows (`module: null`) are shown, not hidden. The
plugin + `.lgx` build green on Linux and macOS in CI. The current view is a flat
connection list; grouping and a graph rendering are the next UI step (and land
more naturally once attribution lights up the `module` labels). See `DESIGN.md`.
