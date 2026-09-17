# Access policy — running netgraph under `enforce`

Logos hosts can gate inter-module calls with an **access policy**: the host
installs one JSON document (via `logos_core_set_access_policy()`, before
`logos_core_start()`; `logoscore` exposes it as `--access-policy <file|inline>`)
that lists, per target module, which callers may invoke it.

```jsonc
{
  "version": 1,
  "mode": "enforce",                       // only "enforce" activates gating
  "restrictions": {
    "<target>": { "allowedCallers": ["<caller>", ...] }
  }
}
```

A target **absent** from `restrictions` is unrestricted (any caller). A target
that is present rejects every caller outside its `allowedCallers`. Enforcement is
done by `capability_module`, which won't mint a call token for a disallowed
caller. (Contract: `liblogos` `logos_core.h`, `logos_core_set_access_policy`.)

## The netgraph entry

[`access-policy.example.json`](access-policy.example.json) is the entry netgraph
needs under `enforce`:

```json
{
  "version": 1,
  "mode": "enforce",
  "restrictions": {
    "netgraph_module": { "allowedCallers": ["netgraph_ui"] }
  }
}
```

Why this specific entry:

- **`netgraph_ui` must be listed explicitly.** It is the only in-tree caller of
  `netgraph_module` (it forwards `setEnabled` / `snapshot` / `getInfo`). But a
  `ui_qml` plugin is **not tracked as a dependent** of the backend it drives — so
  a host that derives its allowlists from the module dependency graph will not
  add `netgraph_ui` on its own. Without this entry, an `enforce` host silently
  blocks the view from reaching the backend and the graph stays empty.
- **The backend's own dependencies need no entry here.** `netgraph_module`
  binds `connection_source` and `core_service` as *interface_dependencies*
  (declared in `module/metadata.json`), which the host allows under `enforce`
  without a `restrictions` line — this file restricts callers *of*
  `netgraph_module`, not the interfaces it calls.
- **Merge, don't replace.** A real deployment runs one policy for every module.
  Fold `"netgraph_module"` into that document's `restrictions` alongside the
  other targets (e.g. `package_manager`, `package_downloader`, …); don't ship
  this file as the whole policy.

## Status of verification

- The **policy shape and the entry** are confirmed against the `liblogos`
  `logos_core.h` contract, and `logoscore --access-policy` is confirmed to accept
  a file or inline JSON.
- A **live end-to-end enforce run is not yet exercised in CI.** Driving the real
  caller path requires `netgraph_ui` running in `ui-host` (a headless
  `logoscore call` is a different caller identity, so it cannot stand in for the
  view). The view itself is confirmed rendering and collecting in Basecamp
  (without an enforce policy); what remains is proving the allowlist gates it.
  Verify manually under Basecamp / a release `logoscore`: install both
  `.#lgx-portable` packages, start the host with `--access-policy
  access-policy.example.json`, open the netgraph view, turn collection on, and
  confirm rows appear (the call is allowed). Flip the entry to a bogus caller and
  confirm the view can no longer read the backend (the call is denied) — that is
  the enforce proof.

See [`DESIGN.md`](DESIGN.md) "Handoff points" #4.
