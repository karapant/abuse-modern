# Abuse Modern

License: **GPL-2.0-or-later** for the combined engine; original per-file notices are retained.

An incremental modernization of the Abuse 0.8 engine, using CMake, C++17,
SDL3 and SDL3_mixer. Original gameplay, software lighting, palette effects,
Lisp scripts and map formats are retained. This is a source port and engine
foundation, with substantial inherited code; it is not a newly written engine.

The original distribution beside this folder is untouched. This folder is the
root of the new repository. Required engine sources under `src/` belong in Git;
original graphics, levels, scripts, music and sounds are external runtime data.

## Build

You need a C++17 compiler, CMake 3.24+, Git, pkg-config and SDL platform
development libraries. The setup helper detects Linux package managers and macOS
Homebrew. It prints a plan by default and only installs with `--install`:

```sh
./scripts/setup.sh --plan --music
./scripts/setup.sh --install --music
./scripts/setup.sh --check
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Ubuntu/Debian, the equivalent manual setup is:

```sh
sudo apt install build-essential cmake git pkg-config ninja-build \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev libxtst-dev libasound2-dev libpulse-dev libgl-dev
sudo apt install freepats  # optional runtime instruments for music
```

The script also provides build-dependency plans for Fedora (`dnf`), Arch (`pacman`),
openSUSE (`zypper`) and macOS (`brew`). On macOS, install Apple command-line tools
with `xcode-select --install` before using the Homebrew install path. Music outside
apt systems needs a TiMidity-compatible `.pat` set and a valid `TIMIDITY_CFG`;
`--music` explains that manual step rather than installing an incompatible SoundFont.
See [SDL's Linux dependency reference](https://wiki.libsdl.org/SDL3/README-linux)
for additional optional platform backends.

CMake uses installed SDL3 >= 3.4 and SDL3_mixer >= 3.2 when available; otherwise
it downloads pinned revisions of SDL 3.4.0 and SDL_mixer 3.2.0 into the ignored
build directory. The first configure therefore needs internet access.
Use `-DABUSE_FETCH_DEPS=OFF` to require installed dependencies. For offline builds,
provide source directories with `-DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL`
and `-DFETCHCONTENT_SOURCE_DIR_SDL3_MIXER=/path/to/SDL_mixer`.

The inherited POSIX filesystem/network code has not been ported to Windows.
Linux is verified locally; CI also builds and tests macOS. Native Windows work is deferred.

## Run with external game data

Game-data source: [Abuse downloads](http://abuse.zoy.org/wiki/download).
Download and extract the game data separately from this engine. Point `-datadir`
at the extracted directory containing `abuse.lsp`, or copy that directory's contents
into this repository's ignored `data/` folder:

```sh
./build/abuse -datadir ./data
```

From this folder, using the original distribution next to it:

```sh
./build/abuse -datadir ../data
```

Use an absolute path if launching from another working directory. Alternatively,
put a local copy of the game data in `data/` (ignored by Git), set `ABUSE_PATH`,
or configure `-DABUSE_ASSET_DIR=/absolute/path/to/data` when building. Data must
include `abuse.lsp` and its referenced scripts, art and maps.

Useful options:

```sh
./build/abuse --help
./build/abuse -datadir ../data -fullscreen
./build/abuse -datadir ../data -nosound
./build/abuse -datadir ../data -edit
```

WASD moves/jumps/uses; mouse aims; left click fires; right click uses the special
ability; mouse wheel changes weapons. F11 toggles desktop fullscreen, F12 toggles
mouse grab, and Print Screen saves `screenshot.bmp` in the working directory.
The main/pause menu uses visible text options with mouse hover, single-click
activation, arrow keys and Enter. Escape resumes a paused game or goes back.
Settings exposes difficulty, sound/music percentages, fullscreen and brightness;
Left/Right adjusts values, and Enter/click increases them. Key bindings can be
changed in Settings; duplicate keys swap actions, Escape cancels capture, and the
fixed menu/help/weapon shortcut keys remain reserved. Changes are saved to
`controls.cfg` and take effect immediately. Restore defaults resets keyboard bindings. Difficulty and audio
changes currently apply to the running session; brightness uses the existing
saved gamma setting. Load game stays visible but disabled when there are no saves.
Save consoles and Load game now use scrolling text lists for all 15 slots,
including Empty/Saved/Unreadable states and overwrite confirmation. Saving still
requires the original in-level consoles; this adds no save-anywhere action.
Multiplayer has text host settings, LAN discovery, direct hostname/IPv4 joining,
and readable validation/errors. Network config is stored under the user save directory.
Quit confirmation uses explicit text choices. Menus stay open until you choose an
option, without the original ten-second automatic demo.

The initial window is centered on its monitor and is 960×600; `-scale 1` through `-scale 8` changes its size.
Resizable presentation uses nearest-neighbor filtering and letterboxing, with
square pixels. Integer-only scaling and 4:3 aspect correction are future options.

Settings and saves use SDL's preference directory for `abuse-modern/abuse-modern`,
separate from the original game's `~/.abuse`. On Linux this is normally
`~/.local/share/abuse-modern/abuse-modern/`. The initial bindings come from `abuserc`; in-game changes in `controls.cfg` take
precedence. Set `ABUSE_SAVE_PATH=/path/to/saves/` to choose a different directory.
`ABUSE_PATH` overrides both the configured data directory and `-datadir`.
MIDI music uses the TiMidity decoder and needs an installed TiMidity patch set
and configuration (for example the `freepats` package); WAV effects
work independently. Original HMI music is converted to MIDI before decoding.
Set `TIMIDITY_CFG=/path/to/freepats.cfg` if the configuration is outside the
standard `/etc/timidity` locations. A SoundFont-only configuration is not a
substitute for the instrument patches used by the bundled TiMidity decoder.

## Gamepad controls

A controller must report both left and right stick axes to SDL for controller-only
play. A missing stick produces a warning and disables controller gameplay; menu
navigation and keyboard/mouse input still work. Detection depends on the device's
SDL mapping. Settings -> Controller shows device/capability status, an enable toggle,
a persistent dead-zone adjustment, and a button guide.

| Input | Action |
| --- | --- |
| Left stick / D-pad | Move |
| Right stick | Aim independently of movement |
| Right trigger | Fire |
| South button (A / Cross) | Jump; confirm menus |
| East button (B / Circle) | Use/crouch; cancel menus |
| Left trigger / West button | Special action |
| Bumpers | Previous/next weapon; page through menus |
| Start | Pause/back |

Aim direction remains when the right stick returns to center. Moving the mouse
takes over aiming; moving the stick again takes it back. Disconnect, focus loss,
disabling the controller and leaving a dialog clear or suppress held actions.
The layout uses SDL's standard gamepad mapping, not device-specific button numbers.
Network text fields currently require a keyboard.

## Extended tests with original assets

Normal CI runs without game assets or music patches. To also test real engine
menus, saving/loading, the one-stick warning, and original intro music:

```sh
cmake -S . -B build \
  -DABUSE_TEST_ASSET_DIR=/absolute/path/to/data \
  -DABUSE_TEST_TIMIDITY_CFG=/absolute/path/to/freepats.cfg
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The scripted engine tests use Linux linker wrapping and a dummy display; intro
music uses a dummy audio device. Fixtures and screenshots stay in the build tree.
No tests overwrite your real saves or modify the asset directory.

## New GitHub repository

Initialize and commit **inside this folder**, so the surrounding original Abuse
distribution is outside the repository:

```sh
cd /path/to/abuse/abuse-modern
git init -b main
git add .
git commit -m "Start Abuse Modern with CMake and SDL3"
```

Then create an empty GitHub repository and follow its commands to add `origin`
and push. The local checkpoint does not create a remote GitHub repository or push code.
The `.gitignore` excludes builds, fetched dependencies, local game data and saves.
It deliberately includes the engine sources necessary to build a fresh clone.
GitHub Actions builds Linux with GCC and Clang and also checks macOS. Tests are asset-free by default.

## Scope and attribution

This milestone replaces SDL1 video, events, mouse handling and SDL_mixer audio;
adds a standalone CMake build and platform regression checks; and preserves the
original renderer and simulation timing. It does not implement interpolated
rendering or redesigned multiplayer transport. The main, pause, settings, save/load
and network dialogs use a text interface, with persistent key rebinding and native
SDL3 gamepad input. Editor-specific tools retain their existing UI.

## License

The combined engine is distributed under **GNU GPL version 2 or, at your option,
any later version** (`GPL-2.0-or-later`). See [LICENSE](LICENSE) and the original per-file license notices. It cannot be distributed under MIT
alone because it incorporates and modifies the GPL-covered SDL port.

Original public-domain and WTFPL files retain their notices. SDL3, SDL3_mixer and
their bundled components have their own licenses, retained in fetched source trees.
Game assets and MIDI instrument patches are separate downloads and are not covered
by the engine's license. Preserve their respective notices when redistributing them.
