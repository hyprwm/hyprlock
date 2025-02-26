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
  hyprlang,
  hyprutils,
  pam,
  pango,
  wayland,
  wayland-protocols,
  wayland-scanner,
  openssl,
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
    libjpeg
    libwebp
    libxkbcommon
    mesa
    hyprlang
    hyprutils
    pam
    pango
    wayland
    wayland-protocols
    openssl
  ];

  meta = {
    homepage = "https://github.com/hyprwm/hyprlock";
    description = "A gpu-accelerated screen lock for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprlock";
  };
}
