{
  description = "netgraph — a Logos module that observes every network connection the Logos process tree holds, grouped by service and network";

  # Pull pre-built artifacts from the self-hosted Logos Attic cache (Nix binary cache).
  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  inputs = {
    # Pinned to the same known-good builder openmetrics-module uses. Bump when the
    # M0 collector needs a newer generator; keep flake.lock committed.
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.5";
  };

  # One module: the `netgraph` observer (sources at the repo/dir root, next to
  # this flake). It declares an interface_dependency on `connection_source`
  # (interfaces/connection_source.h) — no concrete module dependency — and binds
  # it to operator-chosen module names at runtime, so it ships before any module
  # implements the provider side.
  #
  # mkLogosModule exposes the usual per-system outputs: `default` (the plugin),
  # `lgx` (a ready-to-install package), `install` (a modules/ tree), etc.
  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;

      # Pure unit tests for the parse + merge + sweep layer live under tests/ and
      # run without the SDK via tests/run_local.sh (see tests/README.md). Wiring
      # them as a flake check (tests = { dir = ./tests; }) waits on confirming the
      # builder's test-harness contract — the local runner is the proof today.
    };
}
