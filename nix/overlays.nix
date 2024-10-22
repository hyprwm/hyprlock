{
  lib,
  inputs,
}: let
  mkDate = longDate: (lib.concatStringsSep "-" [
    (builtins.substring 0 4 longDate)
    (builtins.substring 4 2 longDate)
    (builtins.substring 6 2 longDate)
  ]);

  version = lib.removeSuffix "\n" (builtins.readFile ../VERSION);
in {
  default = inputs.self.overlays.hyprlock;

  hyprlock = lib.composeManyExtensions [
    inputs.hyprlang.overlays.default
    inputs.hyprutils.overlays.default
    inputs.self.overlays.sdbuscpp
    (final: prev: {
      hyprlock = prev.callPackage ./default.nix {
        stdenv = prev.gcc13Stdenv;
        version = version + "+date=" + (mkDate (inputs.self.lastModifiedDate or "19700101")) + "_" + (inputs.self.shortRev or "dirty");
        inherit (final) hyprlang;
      };
    })
  ];

  sdbuscpp = final: prev: {
    sdbus-cpp = prev.sdbus-cpp.overrideAttrs (self: super: {
      version = "2.0.0";

      src = final.fetchFromGitHub {
        owner = "Kistler-group";
        repo = "sdbus-cpp";
        rev = "refs/tags/v${self.version}";
        hash = "sha256-W8V5FRhV3jtERMFrZ4gf30OpIQLYoj2yYGpnYOmH2+g=";
      };
    });
  };
}
