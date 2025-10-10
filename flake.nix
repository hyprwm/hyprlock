{
  description = "Hyprland's GPU-accelerated screen locking utility";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    systems.url = "github:nix-systems/default-linux";

    hyprgraphics = {
      url = "github:hyprwm/hyprgraphics";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.systems.follows = "systems";
      inputs.hyprutils.follows = "hyprutils";
    };

    hyprutils = {
      url = "github:hyprwm/hyprutils";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.systems.follows = "systems";
    };

    hyprlang = {
      url = "github:hyprwm/hyprlang";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.systems.follows = "systems";
      inputs.hyprutils.follows = "hyprutils";
    };

    hyprwayland-scanner = {
      url = "github:hyprwm/hyprwayland-scanner";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.systems.follows = "systems";
    };
  };

  outputs = {
    self,
    nixpkgs,
    systems,
    ...
  } @ inputs: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);
    pkgsFor = eachSystem (system:
      import nixpkgs {
        localSystem.system = system;
        overlays = [self.overlays.default];
      });
    pkgsDebugFor = eachSystem (system:
      import nixpkgs {
        localSystem = system;
        overlays = [self.overlays.hyprlock-debug];
      });
  in {
    overlays = import ./nix/overlays.nix {inherit inputs lib self;};

    packages = eachSystem (system: {
      default = self.packages.${system}.hyprlock;
      inherit (pkgsFor.${system}) hyprlock;
      inherit (pkgsDebugFor.${system}) hyprlock-debug hyprlock-test-meta;
    });

    homeManagerModules = {
      default = self.homeManagerModules.hyprlock;
      hyprlock = builtins.throw "hyprlock: the flake HM module has been removed. Use the module from Home Manager upstream.";
    };

    checks = eachSystem (system: self.packages.${system} // (import ./nix/tests/default.nix inputs pkgsFor.${system}));

    formatter = eachSystem (system: pkgsFor.${system}.alejandra);
  };
}
