{
  description = "netgraph_ui — the QML view for netgraph_module: a live, local-only table of every connection the Logos process tree holds";

  # Pull pre-built artifacts from the self-hosted Logos Attic cache (Nix binary cache).
  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  inputs = {
    # Same known-good builder the backend (module/) pins. Keep flake.lock committed.
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.5";

    # The backend this view drives. Name MUST match metadata.json "dependencies".
    # Defaults to the published module/ tree; for co-development build with
    #   nix build --override-input netgraph_module path:../module
    # (nix rejects a relative path here, so the committed default is the github ref).
    netgraph_module.url = "github:corpetty/netgraph-module?dir=module";
    netgraph_module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  # UI modules build with mkLogosQmlModule (not mkLogosModule). Dependencies are
  # flake inputs whose names match metadata.json "dependencies" — auto-resolved
  # and auto-bundled at build time.
  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
