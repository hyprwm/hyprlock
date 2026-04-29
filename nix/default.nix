{
  lib,
  stdenv,
  cmake,
  pkg-config,
  cairo,
  ffmpeg ? null,
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
  withVideoBackend ? true,
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
  ] ++ lib.optionals withVideoBackend [ ffmpeg ];

  cmakeFlags = lib.mapAttrsToList lib.cmakeFeature {
    HYPRLOCK_COMMIT = shortRev;
    HYPRLOCK_VERSION_COMMIT = ""; # Intentionally left empty (hyprlock --version will always print the commit)
  } ++ lib.optional (!withVideoBackend) (lib.cmakeBool "VIDEO_BACKEND" false);

  meta = {
    homepage = "https://github.com/hyprwm/hyprlock";
    description = "A gpu-accelerated screen lock for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprlock";
  };
}
