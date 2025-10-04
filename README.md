# hyprlock
Hyprland's simple, yet multi-threaded and GPU-accelerated screen locking utility.

## Features
 - Uses the ext-session-lock protocol
 - Support for fractional-scale
 - Fully GPU accelerated
 - Multi-threaded resource acquisition
 - Blurred screenshot as the background
 - Native fingerprint support (using libfprint's dbus interface)
 - Some of Hyprland's eyecandy: gradient borders, blur, animations, shadows, etc.
 - and more...

## How it looks

![](https://i.ibb.co/8Bd98BP/20240220-00h12m46s.png)

## Docs / Configuration
[See the wiki](https://wiki.hyprland.org/Hypr-Ecosystem/hyprlock/)

## Arch install
```sh
pacman -S hyprlock # binary x86 tagged release
# or
yay -S hyprlock-git # compiles from latest source
```

## Building

### Deps
You need the following dependencies

- cairo
- hyprgraphics
- hyprlang
- hyprutils
- hyprwayland-scanner
- mesa (required is libgbm, libdrm and the opengl runtime)
- pam
- pango
- sdbus-cpp (>= 2.0.0)
- wayland-client
- wayland-protocols
- xkbcommon

Sometimes distro packages are missing required development files.
Such distros usually offer development versions of library package - commonly suffixed with `-devel` or `-dev`.

### Building

Building:
```sh
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -S . -B ./build
cmake --build ./build --config Release --target hyprlock -j`nproc 2>/dev/null || getconf _NPROCESSORS_CONF`
```

Installation:
```sh
sudo cmake --install build
```
