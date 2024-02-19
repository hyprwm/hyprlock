# hyprlock
Hyprland's simple, yet multi-threaded and GPU-accelerated screen locking utility.

## Features
 - uses the secure ext-session-lock protocol
 - full support for fractional-scale
 - fully GPU accelerated
 - multi-threaded resource acquisition for no hitches

## How it looks

![](https://i.ibb.co/HXF8pY3/20240218-23h08m26s.png)

## Docs / Configuration
[See the wiki](https://wiki.hyprland.org/Hypr-Ecosystem/hyprlock/)

## Building

### Deps
 - wayland-client
 - wayland-protocols
 - cairo
 - gles3.2
 - pango
 - hyprlang>=0.4.0
 - xkbcommon
 - pam

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
