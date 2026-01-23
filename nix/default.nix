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
  hyprauth,
  hyprgraphics,
  hyprlang,
  hyprutils,
  hyprwayland-scanner,
  pam,
  pango,
  sdbus-cpp_2,
  systemdLibs,
  wayland,
  wayland-protocols,
  wayland-scanner,
  debug ? false,
  withMold ? true,
  version ? "git",
  shortRev ? "",
}:
let
  inherit (builtins) foldl';
  inherit (lib.lists) flatten optional;
  inherit (lib.strings) optionalString;

  adapters = flatten [
    (lib.optional withMold stdenvAdapters.useMoldLinker)
    (lib.optional debug stdenvAdapters.keepDebugInfo)
  ];

  customStdenv = foldl' (acc: adapter: adapter acc) stdenv adapters;
in
customStdenv.mkDerivation {
  pname = "hyprlock" + optionalString debug "-debug";
  inherit version;

  src = ../.;

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
    hyprauth
    hyprgraphics
    hyprlang
    hyprutils
    pam
    pango
    sdbus-cpp_2
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
    else "RelWithDebInfo";

  meta = {
    homepage = "https://github.com/hyprwm/hyprlock";
    description = "A gpu-accelerated screen lock for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprlock";
  };
}
