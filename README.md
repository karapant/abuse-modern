# Abuse Modern

An SDL3 port of Abuse 0.8 with upgraded graphics, widescreen play, redesigned menus, and dual-stick gamepad support.

[Download Abuse Modern](https://github.com/karapant/abuse-modern/releases/latest) · [Project website](https://karapant.github.io/abuse-modern/)

## Install and play

- **Windows x64** — Run the setup `.exe`, then open **Abuse Modern** from the Start menu.
- **Linux x64** — Extract the `.tar.gz` and run `./play` from the extracted folder.
- **macOS** — Open the Apple Silicon or Intel `.dmg` for your Mac, then run **Play Abuse Modern.command**.

On first launch, download the Abuse 0.8 game data through the launcher or import an archive or folder you already have.

Use **WASD** to move, the **mouse** to aim, and **left click** to fire. Press **1–8** to select a weapon directly, **Q** to swap to your last weapon, and **Esc** to pause. Change bindings and controller settings under **Settings**.

## Enhancements

The same scene with enhancements off (left) and on (right):

![Gameplay comparison with enhancements off on the left and on on the right](resources/enhancements-comparison.png)

All enhancements can be toggled under **Settings → Enhancements** (plus controller and binding options under **Settings**):

- **Modern HUD** — Centered health, ammo, and 8 weapon slots that stay easy to scan.
- **Enhanced Lighting** — Warm/cool dynamic fixtures, wall reflections, and approaching save-console glow.
- **Enhanced Projectiles** — Tapered laser/plasma beams and short projectile trails.
- **Combat Feedback** — Reticle hit-markers (with kill confirmation), enemy hit-flashes, compact target health bars, and a crimson damage / low-health edge vignette.
- **Smart Crosshair** — Responsive reticle that pulses on fire, highlights over hurtable enemies, and shows low/empty ammo cues.
- **Quick Weapon Switching** — Direct `1–8` weapon hotkeys, `Q` to swap to your previous weapon, mouse-wheel cycling, and a brief weapon/ammo badge.
- **Checkpoint Feedback** — Save consoles light up as you approach and confirm when progress is saved.
- **Directional Sound Cues** — Subtle edge indicators for off-screen doors, lifts, and enemy alerts.
- **Widescreen, Smooth Motion & Pixel Filter** — Up to `640×360` view resolution, 60fps camera/object interpolation, and an optional smooth pixel filter.
- **Modern Menus & Dual-Stick Support** — Labeled menus, explored automap, save-slot previews, and dual-stick gamepad aiming.

## Build from source

Use CMake 3.24+, Git, and a C++17 compiler. On Linux and macOS, run `./scripts/setup.sh --install` to install dependencies. On Windows, build from the MSYS2 UCRT64 shell with MinGW-w64 GCC, CMake, Ninja, and pkgconf.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/abuse -datadir "/path/to/folder-containing-abuse.lsp"
```

[GPL-2.0-or-later](LICENSE) for the engine. Game data retains its original licensing.
