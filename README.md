# hyprlock
Hyprland's simple, yet multi-threaded and GPU-accelerated screen locking utility.

## Features
 - uses the secure ext-session-lock protocol
 - full support for fractional-scale
 - fully GPU accelerated
 - multi-threaded resource acquisition for no hitches

## Example config

```ini
general {
    disable_loading_bar = false
}

background {
    monitor = DP-2
    path = /home/me/someImage.png
}

background {
    monitor = WL-2
    path = /home/me/someImage2.png
}

input-field {
    monitor =
    size = 200, 50
    outline_thickness = 3
    outer_color = rgb(151515)
    inner_color = rgb(200, 200, 200)
}
```

## Docs

soon:tm:

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
