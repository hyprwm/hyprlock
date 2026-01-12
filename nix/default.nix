{
  lib,
  stdenv,
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
  sdbus-cpp_2,
  systemdLibs,
  wayland,
  wayland-protocols,
  wayland-scanner,
  version ? "git",
  shortRev ? "",
}:
stdenv.mkDerivation {
  pname = "hyprlock";
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

  meta = {
    homepage = "https://github.com/hyprwm/hyprlock";
    description = "A gpu-accelerated screen lock for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprlock";
  };
}
