{
  lib,
  stdenv,
  stdenvAdapters,
  cmake,
  pkg-config,
  cairo,
  libdrm,
  libGL,
  libxkbcommon,
  libgbm,
  hyprgraphics,
  hyprlang,
  hyprutils,
  hyprwayland-scanner,
  pam,
  pango,
  sdbus-cpp,
  systemdLibs,
  wayland,
  wayland-protocols,
  wayland-scanner,
  debug ? false,
  version ? "git",
  shortRev ? "",
}: let
  inherit (builtins) foldl';
  inherit (lib.lists) flatten;
  inherit (lib.sources) cleanSourceWith cleanSource;
  inherit (lib.strings) hasSuffix optionalString;

  adapters = flatten [
    stdenvAdapters.useMoldLinker
    (lib.optional debug stdenvAdapters.keepDebugInfo)
  ];

  customStdenv = foldl' (acc: adapter: adapter acc) stdenv adapters;
  in
customStdenv.mkDerivation {
  pname = "hyprlock${optionalString debug "-debug"}";
  inherit version;

  src = cleanSourceWith {
    filter = name: _type: let
      baseName = baseNameOf (toString name);
    in
      ! (hasSuffix ".nix" baseName);
    src = cleanSource ../.;
  };

  nativeBuildInputs = [
    cmake
    pkg-config
    hyprwayland-scanner
    wayland-scanner
  ];

  buildInputs = [
    cairo
    libdrm
    libGL
    libxkbcommon
    libgbm
    hyprgraphics
    hyprlang
    hyprutils
    pam
    pango
    sdbus-cpp
    systemdLibs
    wayland
    wayland-protocols
  ];

  cmakeFlags = lib.mapAttrsToList lib.cmakeFeature {
    HYPRLOCK_COMMIT = shortRev;
    HYPRLOCK_VERSION_COMMIT = ""; # Intentionally left empty (hyprlock --version will always print the commit)
  };

  cmakeBuildType =
    if debug
    then "Debug"
    else "Release";

  meta = {
    homepage = "https://github.com/hyprwm/hyprlock";
    description = "A gpu-accelerated screen lock for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprlock";
  };
}
