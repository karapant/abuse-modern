# Validation of the initial SDL3 port

Checked on Linux x86-64 on 2026-11-16:

- GCC 15.2 Debug build of `abuse`, `abuse-tool` and `platform-test`.
- Clang 21.1 Release build from a standalone copy under `/tmp`, with no access
  to the original engine source paths through the build configuration.
- SDL 3.4.0 and SDL_mixer 3.2.0 at the revisions in `UPSTREAM.md`.
- Five CTest cases pass with both compilers.
- Temporary Git repository check: engine sources are visible to Git; build
  outputs and game-data directories are excluded.

The platform case exercises full-frame edge pixels, clipped dirty rectangles,
palette changes without redraw, resize/letterbox mouse coordinates, shifted
key releases, focus loss, mouse-wheel press/release pairs and middle-button
press/release transitions. The other cases check headless help, invalid scale,
a missing scale argument and missing external data. Tests use SDL's dummy video
driver and software renderer and do not require original game assets.

Headless startup checks using the supplied original data loaded Lisp and base
graphics, initialized the SDL3 renderer and effect mixer, and remained running
until the check's timeout. An editor startup check used `-edit -f levels/frabs00.spe`.
The music decoder initially reported an error without an instrument configuration;
with FreePats and `TIMIDITY_CFG` pointing to its configuration, that error was resolved.
The audio device was SDL's dummy driver, so these checks do not verify audible output.
Instrument patches were used only for local testing and are not included here.

Visible desktop testing was attempted with Xvfb, but this execution environment
blocked X display connections (`xdpyinfo` also failed). No claim is made that a
visible play session, campaign completion, real audio playback, saves, all editor
operations, multiplayer, macOS or Windows has been verified. Legacy compiler
warnings remain in inherited engine code.

Before a release, run a real play session with the original data, test saves and
loads, toggle fullscreen, resize while aiming, verify sound/music, and exercise
the level editor. GitHub Actions provides compiler/platform regression checks;
it does not run asset-dependent gameplay.

## Text menu update

The platform check also verifies skipping a disabled Load game row, single-click
row activation, ignoring clicks on disabled rows and the footer, Left/Right
adjustments, Escape navigation and startup window centering within the monitor
work area. It writes `menu-preview.bmp` and
`settings-preview.bmp` into the build directory. Pass the original
`art/back/backgrnd.spe` path to `platform-test` to render these previews with the
actual game palette; this optional preview was checked visually at 320×200.

A local headless integration run also drove the actual game through Main menu ->
Settings -> difficulty and volume changes -> Back -> Quit/Cancel -> Quit/Confirm,
using injected SDL events and a temporary linked test driver. It exited normally.
This checks menu integration with the running engine; visible desktop behavior
still needs manual confirmation.
