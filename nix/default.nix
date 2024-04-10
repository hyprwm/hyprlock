{
  lib,
  stdenv,
  cmake,
  pkg-config,
  cairo,
  libdrm,
  libGL,
  libxkbcommon,
  mesa,
  hyprlang,
  pam,
  pango,
  wayland,
  wayland-protocols,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "hyprlock";
  inherit version;

  src = ../.;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    cairo
    libdrm
    libGL
    libxkbcommon
    mesa
    hyprlang
    pam
    pango
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
