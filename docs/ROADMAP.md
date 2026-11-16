# Roadmap after the v0.1 checkpoint

The current checkpoint builds the original Abuse gameplay on CMake/C++17, SDL3
and SDL3_mixer, with text main/pause/settings/quit menus and centered startup
window positioning. Required engine sources and license notices are included;
game assets, builds and local saves remain outside Git.

## Next: persistent settings

- Persist sound/music volume, difficulty and fullscreen selections across launches.
- Keep settings in the engine's separate preference directory.
- Preserve current bindings and handle incomplete configuration files gracefully.
- Verify changes survive restart and command-line options take precedence as documented.

## Graphics experiment

- Compare one original character animation and one environment tileset with a 2x upscale.
- Check animation consistency, transparent edges, tile seams and readability in motion.
- Keep original assets selectable and store experimental art separately from engine source.
- Plan renderer support for higher-resolution art while retaining gameplay coordinates,
  collision geometry and mouse aiming.
- Add integer scaling and optional 4:3 correction before a larger art overhaul.

## Remaining dialogs

- Extend visible text choices to save selection, loading, networking and editor dialogs.
- Keep original checkpoint/save-console behavior; save-anywhere is a separate feature decision.
- Add in-game key rebinding and controller setup after the remaining dialogs are readable.

## Before calling it a release

Complete a visible desktop play test covering movement, aiming, fullscreen,
resizing, audible effects/music, a save at an in-level console, loading that save,
and basic editor operations. Existing headless checks and their limits are recorded
in [VALIDATION.md](VALIDATION.md). Multiplayer and other operating systems need
separate validation.
