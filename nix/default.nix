{
  lib,
  stdenv,
  cmake,
  pkg-config,
  cairo,
  file,
  libdrm,
  libGL,
  libgcrypt,
  libjpeg,
  libwebp,
  libxkbcommon,
  mesa,
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
    libgcrypt
    libjpeg
    libwebp
    libxkbcommon
    mesa
    hyprlang
    hyprutils
    pam
    pango
    sdbus-cpp
    systemdLibs
    wayland
    wayland-protocols
  ];

  meta = {
    homepage = "https://github.com/hyprwm/hyprlock";
    description = "A gpu-accelerated screen lock for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprlock";
  };
}
