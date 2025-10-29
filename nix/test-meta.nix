{
  cmake,
  egl-wayland,
  hyprland-protocols,
  hyprlock,
  hyprwayland-scanner,
  lib,
  pkg-config,
  stdenv,
  stdenvAdapters,
  wayland-scanner,
  version ? "git",
}: let
  inherit (lib.sources) cleanSourceWith cleanSource;
  inherit (lib.strings) hasSuffix;
in
  stdenv.mkDerivation (finalAttrs: {
    pname = "hyprlock-test-meta";
    inherit version;

    src = cleanSourceWith {
      filter = name: _type: let
        baseName = baseNameOf (toString name);
      in
        ! (hasSuffix ".nix" baseName);
      src = cleanSource ../tests;
    };

    nativeBuildInputs = [
      cmake
      hyprland-protocols
      hyprwayland-scanner
      pkg-config
      wayland-scanner
    ];

    buildInputs = hyprlock.buildInputs;

    meta = {
      homepage = "https://github.com/hyprwm/hyprlock";
      description = "Hyprlock testing utility";
      license = lib.licenses.bsd3;
      platforms = hyprlock.meta.platforms;
    };
  })
