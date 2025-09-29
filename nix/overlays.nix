{
  lib,
  inputs,
  self,
}: let
  mkDate = longDate: (lib.concatStringsSep "-" [
    (builtins.substring 0 4 longDate)
    (builtins.substring 4 2 longDate)
    (builtins.substring 6 2 longDate)
  ]);

  version = lib.removeSuffix "\n" (builtins.readFile ../VERSION);
in {
  default = inputs.self.overlays.hyprlock;

  hyprlock-with-deps = lib.composeManyExtensions [
    inputs.hyprgraphics.overlays.default
    inputs.hyprlang.overlays.default
    inputs.hyprutils.overlays.default
    inputs.hyprwayland-scanner.overlays.default
    self.overlays.hyprlock
    self.overlays.lock_tester
    (final: prev: {
      hyprlock = prev.callPackage ./default.nix {
        stdenv = prev.gcc15Stdenv;
        version = version + "+date=" + (mkDate (inputs.self.lastModifiedDate or "19700101")) + "_" + (inputs.self.shortRev or "dirty");
        inherit (final) hyprlang;
        shortRev = self.sourceInfo.shortRev or "dirty";
      };
    })
  ];

  hyprlock = final: prev: {
    hyprlock = prev.callPackage ./default.nix {
      stdenv = prev.gcc15Stdenv;
      version =
        version
        + "+date="
        + (mkDate (inputs.self.lastModifiedDate or "19700101"))
        + "_"
        + (inputs.self.shortRev or "dirty");
      shortRev = self.sourceInfo.shortRev or "dirty";
    };
  };

  hyprlock-debug = lib.composeManyExtensions [
    self.overlays.hyprlock
    # Dependencies
    (final: prev: {
      hyprutils = prev.hyprutils.override {debug = true;};
      hyprlock-debug = prev.hyprlock.override {debug = true;};
    })
  ];

  lock_tester = final: prev: {
    lock_tester = prev.callPackage ./tester.nix {
      stdenv = prev.gcc14Stdenv;
      version = version + "+date=" + (mkDate (inputs.self.lastModifiedDate or "19700101")) + "_" + (inputs.self.shortRev or "dirty");
      hyprland-protocols = final.hyprland-protocols;
      wayland-scanner = final.wayland-scanner;
    };
  };
}
