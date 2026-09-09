{
  description = "conn_fixture_module — a netgraph doc-test fixture that holds a known loopback connection open while loaded, giving the observer a module-owned connection to attribute. Not a shipping module.";

  # Pull pre-built artifacts from the self-hosted Logos Attic cache (Nix binary cache).
  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  inputs = {
    # Same builder pin as the netgraph module next door; keep flake.lock committed
    # so the doc-test can build this from the immutable github source.
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.5";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
