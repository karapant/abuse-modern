# Engine foundation

The first milestone is an incremental source port. Retaining existing object,
collision, Lisp and map systems gives a playable baseline against which later
engine changes can be compared.

## Boundaries

- `src/sdlport/`: SDL3 window, presentation, mouse, input, audio and startup.
  The directory name is inherited; this is native SDL3 code, not an SDL1 shim.
- `src/imlib/`: indexed images, palette, original GUI and file formats.
- `src/lisp/`: game scripting interpreter.
- `src/net/`: inherited POSIX networking; not redesigned or newly validated.
- `src/*.cpp`: simulation, objects, maps, lighting, camera, editor and menus.
- `cmake/`: explicit source lists, platform configuration and pinned dependencies.
- `src/ui/textmenu.*`: shared text menu rendering, navigation and row hit targets.
- `tests/`: asset-free checks using SDL's dummy video and software renderer.

The game still draws into an 8-bit indexed image. Dirty rectangles copy into a
retained framebuffer, then each presentation converts palette indices to ARGB
and uploads one SDL streaming texture. Re-converting on palette changes preserves
fades without requiring the game to dirty every pixel. SDL handles scaling,
letterboxing and high-DPI output. Mouse coordinates use SDL's inverse presentation
transform, including letterbox offsets.

Logical dimensions remain 320×200 by default. Resizing changes presentation,
not the visible world or simulation. The legacy editor-only `-size W H` option explicitly
changes logical dimensions; the game still restricts that option to editor mode.

SDL3_mixer owns a pool of 50 sound-effect tracks and individual song tracks.
Effects decode from memory; original HMI songs are converted to MIDI. Decoder
availability and MIDI patch configuration determine whether music can play.

Settings use a separate SDL preference directory. Configuration parsing validates
numeric settings and ignores blank/comment/incomplete lines. Command-line data
paths accept spaces. `--help` runs without SDL video initialization or game data.

## Follow-up milestones

1. Validate a real play session: campaign, saves, editor, fullscreen, audio and mods.
2. Add integer-only scaling, 4:3 correction and persisted presentation settings.
3. Add gamepad bindings, in-game rebinding and persisted settings; extend the
   text interface to remaining save/network/editor dialogs.
4. Separate fixed simulation updates from presentation, then add interpolation.
   Preserve original simulation cadence and assess scripts/collision before changing it.
5. Improve resource ownership and file-parser robustness, guided by sanitizers.
6. Port remaining POSIX filesystem/networking assumptions and validate macOS/Windows.
7. Improve mod discovery and editor workflows; consider multiplayer separately.

This project currently inherits legacy raw pointers, file parsers, legacy secondary dialogs
and simulation behavior. A successful platform test is not evidence that every
campaign level, addon, editor action or multiplayer path has been validated.
