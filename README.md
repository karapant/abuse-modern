# Abuse Modern

An incremental modernization of the Abuse 0.8 engine, using CMake, C++17,
SDL3 and SDL3_mixer. Original gameplay, software lighting, palette effects,
Lisp scripts and map formats are retained. This is a source port and engine
foundation, with substantial inherited code; it is not a newly written engine.

The original distribution beside this folder is untouched. This folder is the
root of the new repository. Required engine sources under `src/` belong in Git;
original graphics, levels, scripts, music and sounds are external runtime data.

## Build

Linux is the verified platform. A C++17 compiler, CMake 3.24+, Git and platform
SDL development dependencies are required. On Ubuntu, for example:

```sh
sudo apt install build-essential cmake git pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev libasound2-dev libpulse-dev libgl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake uses installed SDL3 >= 3.4 and SDL3_mixer >= 3.2 when available; otherwise
it downloads pinned revisions of SDL 3.4.0 and SDL_mixer 3.2.0 into the ignored
build directory. The first configure therefore needs internet access.
Use `-DABUSE_FETCH_DEPS=OFF` to require installed dependencies. For offline builds,
provide source directories with `-DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL`
and `-DFETCHCONTENT_SOURCE_DIR_SDL3_MIXER=/path/to/SDL_mixer`.

The inherited POSIX filesystem/network code has not been ported to Windows.
macOS is a future validation target; it is not currently covered by CI.

## Run with external game data

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
Left/Right adjusts values, and Enter/click increases them. Difficulty and audio
changes currently apply to the running session; brightness uses the existing
saved gamma setting. Load game stays visible but disabled when there are no saves.
Quit confirmation uses explicit text choices. Menus stay open until you choose an
option, without the original ten-second automatic demo.

The initial window is centered on its monitor and is 960×600; `-scale 1` through `-scale 8` changes its size.
Resizable presentation uses nearest-neighbor filtering and letterboxing, with
square pixels. Integer-only scaling and 4:3 aspect correction are future options.

Settings and saves use SDL's preference directory for `abuse-modern/abuse-modern`,
separate from the original game's `~/.abuse`. On Linux this is normally
`~/.local/share/abuse-modern/abuse-modern/`. Edit `abuserc` to change bindings or
set `ABUSE_SAVE_PATH=/path/to/saves/` to choose a different directory.
`ABUSE_PATH` overrides both the configured data directory and `-datadir`.
MIDI music uses the TiMidity decoder and needs an installed TiMidity patch set
and configuration (for example the `freepats` package); WAV effects
work independently. Original HMI music is converted to MIDI before decoding.
Set `TIMIDITY_CFG=/path/to/freepats.cfg` if the configuration is outside the
standard `/etc/timidity` locations. A SoundFont-only configuration is not a
substitute for the instrument patches used by the bundled TiMidity decoder.

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
GitHub Actions builds with GCC and Clang and runs asset-free platform checks.

## Scope and attribution

This milestone replaces SDL1 video, events, mouse handling and SDL_mixer audio;
adds a standalone CMake build and platform regression checks; and preserves the
original renderer and simulation timing. It does not implement interpolated
rendering, controller bindings or redesigned multiplayer. The main, pause,
settings and quit menus use a new text interface; secondary save/network/editor
dialogs still use the original UI.

See the [next-task roadmap](docs/ROADMAP.md), [validation results](docs/VALIDATION.md), [architecture and roadmap](docs/ARCHITECTURE.md),
[upstream provenance](docs/UPSTREAM.md) and [licensing](COPYING).
Original copyright notices, author credits and license texts are retained.
