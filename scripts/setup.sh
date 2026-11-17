#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Print a dependency plan by default; installation is always explicit.
set -eu
mode=plan
manager=
music=0
usage() {
    cat <<'HELP'
Usage: scripts/setup.sh [--plan | --check | --install] [--music]
       scripts/setup.sh --plan --manager apt|dnf|pacman|zypper|brew

--plan     Show package commands without installing anything (default).
--check    Check build tools and report MIDI patch configuration.
--install  Install build dependencies with the detected package manager.
--music    Include FreePats on apt systems; otherwise show portable patch setup.

SDL3 and SDL3_mixer are fetched by CMake when absent. Original game data is
separate. Linux and macOS are supported build paths; native Windows is not ported.
HELP
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --plan) mode=plan ;;
        --check) mode=check ;;
        --install) mode=install ;;
        --music) music=1 ;;
        --manager) shift; [ "$#" -gt 0 ] || { usage >&2; exit 2; }; manager=$1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done
if [ -n "$manager" ] && [ "$mode" != plan ]; then
    echo '--manager is available only for a non-installing --plan.' >&2; exit 2
fi
music_info() {
    config=${TIMIDITY_CFG:-}
    if [ -n "$config" ]; then
        if [ -r "$config" ]; then echo "MIDI configuration: $config"; return 0; fi
        echo "TIMIDITY_CFG is not readable: $config" >&2; return 1
    fi
    for config in /etc/timidity/freepats.cfg /etc/timidity/timidity.cfg /etc/timidity.cfg /opt/local/etc/timidity/timidity.cfg /opt/homebrew/etc/timidity.cfg /usr/local/etc/timidity.cfg; do
        if [ -r "$config" ]; then echo "MIDI configuration found: $config"; return 0; fi
    done
    echo 'Music needs TiMidity-compatible .pat instruments (such as classic FreePats).'
    echo 'Set TIMIDITY_CFG=/absolute/path/to/freepats.cfg and ensure its dir/source paths are valid.'
    echo 'A SoundFont-only (.sf2) configuration is not sufficient for this decoder.'
    return 1
}
if [ "$mode" = check ]; then
    result=0
    for tool in git cmake pkg-config; do
        if command -v "$tool" >/dev/null 2>&1; then echo "Found $tool"; else echo "Missing $tool"; result=1; fi
    done
    if command -v c++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1; then echo 'Found C++ compiler'; else echo 'Missing C++ compiler'; result=1; fi
    if command -v cmake >/dev/null 2>&1; then
        version=$(cmake --version | sed -n '1s/.*version //p')
        major=${version%%.*}; remainder=${version#*.}; minor=${remainder%%.*}
        if [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 24 ]; }; then
            echo 'CMake 3.24 or newer is required.'; result=1
        fi
    fi
    if ! music_info && [ "$music" -eq 1 ]; then result=1; fi
    exit "$result"
fi
if [ -z "$manager" ]; then
    case $(uname -s) in
        Darwin) manager=brew ;;
        Linux)
            for candidate in apt-get dnf pacman zypper; do
                if command -v "$candidate" >/dev/null 2>&1; then manager=$candidate; break; fi
            done ;;
        *) echo 'Unsupported platform. See README.md for supported build paths.' >&2; exit 2 ;;
    esac
fi
case "$manager" in
    apt|apt-get)
        manager=apt-get
        set -- install -y build-essential cmake git pkg-config ninja-build libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libasound2-dev libpulse-dev libgl-dev
        if [ "$music" -eq 1 ]; then set -- "$@" freepats; fi ;;
    dnf) set -- install -y gcc-c++ cmake git pkgconf-pkg-config ninja-build libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXfixes-devel libXScrnSaver-devel libXtst-devel alsa-lib-devel pulseaudio-libs-devel mesa-libGL-devel ;;
    pacman) set -- -S --needed base-devel cmake git pkgconf ninja libx11 libxext libxrandr libxcursor libxi libxfixes libxss libxtst alsa-lib libpulse mesa ;;
    zypper) set -- install -y gcc-c++ cmake git pkg-config ninja libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXfixes-devel libXss-devel libXtst-devel alsa-devel libpulse-devel Mesa-libGL-devel ;;
    brew) set -- install cmake git pkgconf ninja ;;
    *) echo "Unsupported package manager: $manager" >&2; exit 2 ;;
esac
printf 'Dependency command: '
if [ "$manager" != brew ]; then printf 'sudo '; fi
printf '%s ' "$manager" "$@"; printf '\n'
if [ "$mode" = install ]; then
    command -v "$manager" >/dev/null 2>&1 || { echo "Install $manager first." >&2; exit 1; }
    if [ "$manager" = brew ]; then
        xcode-select -p >/dev/null 2>&1 || { echo 'Install Apple command-line tools: xcode-select --install' >&2; exit 1; }
        "$manager" "$@"
    else
        if [ "$(id -u)" -eq 0 ]; then "$manager" "$@"; else sudo "$manager" "$@"; fi
    fi
fi
if [ "$music" -eq 1 ] && [ "$manager" != apt-get ]; then music_info || true; fi
echo 'Next: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release'
echo '      cmake --build build --parallel'
echo '      ctest --test-dir build --output-on-failure'
echo '      ./build/abuse -datadir /path/to/original/data'
