# hyprlock
Hyprland's simple, yet multi-threaded and GPU-accelerated screen locking utility.

## Features
 - uses the secure ext-session-lock protocol
 - full support for fractional-scale
 - fully GPU accelerated
 - multi-threaded resource acquisition for no hitches

## How it looks

![](https://i.ibb.co/8Bd98BP/20240220-00h12m46s.png)

## Docs / Configuration
[See the wiki](https://wiki.hyprland.org/Hypr-Ecosystem/hyprlock/)

## Building

### Deps
Hyprlock requires `hyprlang` >= 0.4.0. See the 
[README](https://github.com/hyprwm/hyprlang/blob/main/README.md#building-and-installation) for 
build instructions.

You also need the following dependencies
 - wayland-client
 - wayland-protocols
 - mesa
 And the development libraries for the following:-
 - cairo
 - libdrm
 - pango
 - xkbcommon
 - pam
Development libraries are usually suffixed with `-devel` or `-dev` in most distro repos.

You also need to install `mesa-libgdm-devel` on some distros like RPM based ones where its not
bundled with the mesa package.

### Building

Building:
```sh
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -S . -B ./build
cmake --build ./build --config Release --target hyprlock -j`nproc 2>/dev/null || getconf NPROCESSORS_CONF`
```

Installation:
```sh
sudo cmake --install build
```
