{
  lib,
  stdenv,
  cmake,
  pkg-config,
  cairo,
  file,
  libdrm,
  libGL,
  libjpeg,
  libwebp,
  libxkbcommon,
  mesa,
  hyprgraphics,
  hyprlang,
  hyprutils,
  pam,
  pango,
  sdbus-cpp,
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
    wayland-scanner
  ];

  buildInputs = [
    cairo
    file
    libdrm
    libGL
    libjpeg
    libwebp
    libxkbcommon
    mesa
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
    HYPRLOCK_VERSION_COMMIT = shortRev;
  };

  meta = {
    homepage = "https://github.com/hyprwm/hyprlock";
    description = "A gpu-accelerated screen lock for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprlock";
  };
}
