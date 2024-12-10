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

### Password hash configuration
If PAM authentication is unavailable to you, you can use password hash authentication via `libgcrypt`.
Activated it by setting `general:password_hash` to the desired value as a string of hexadecimal numbers.
You can select the hash function with `general:password_hash` with the default being `SHA256`.
Other known hash functions are `SHA3-256`, `SHA512_256` or `SHAKE128`.
You can also salt the by setting `hash_salt`.
Set an individual salt (and matching hash) on different systems or across different users to possibly mask that you/users are using the same password.

You can set up a new password hash by first selecting the hash function (e.g. `SHA3-256`) and then using OpenSSL to create the salt and hash:
``` sh
# Produces 10 bytes salt
SALT=$(openssl rand -hex 10)
printf "hash_salt = %s\n" "$SALT"
# Enter your password (no echo) and press ENTER.
{ read -s v; echo "$v${SALT}" } | openssl sha3-256 -hex
```

## Arch install
```sh
pacman -S hyprlock # binary x86 tagged release
# or
yay -S hyprlock-git # compiles from latest source
```

## Building

### Deps
You need the following dependencies
- wayland-client
- wayland-protocols
- mesa

And the development libraries for the following
- cairo
- libdrm
- pango
- xkbcommon
- pam
- hyprlang
- hyprutils
- hyprgraphics
- libmagic (file-devel on Fedora)

Development libraries are usually suffixed with `-devel` or `-dev` in most distro repos.

You also need to install `mesa-libgbm-devel` on some distros like RPM based ones where its not
bundled with the mesa package.

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
