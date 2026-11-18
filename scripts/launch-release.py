#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install external Abuse data and start a packaged Abuse Modern engine."""

import argparse
import hashlib
import http.client
import os
from pathlib import Path, PurePosixPath
import shutil
import ssl
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
from urllib.parse import urlsplit
from urllib.request import urlopen
import zipfile


UPSTREAM = "https://abuse.zoy.org/raw-attachment/wiki/download/abuse-0.8.tar.gz"
# FreeBSD games/abuse_sdl distinfo identifies the original 0.8 release archive.
UPSTREAM_SHA256 = "0104db5fd2695c9518583783f7aaa7e5c0355e27c5a803840a05aef97f9d3488"
REQUIRED = ("abuse.lsp", "lisp/startup.lsp", "art/back/backgrnd.spe", "levels/level00.spe")
MAX_DOWNLOAD = 64 * 1024 * 1024
MAX_EXPANDED = 256 * 1024 * 1024
MAX_ENTRIES = 20000


def data_location():
    home = Path.home()
    if sys.platform == "darwin":
        root = home / "Library/Application Support"
    elif sys.platform == "win32":
        root = Path(os.environ.get("LOCALAPPDATA") or home / "AppData/Local")
    else:
        root = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    return root / "abuse-modern" / "data"


def is_game_data(path):
    return all((path / name).is_file() for name in REQUIRED)


def find_game_data(path):
    for candidate in (path, path / "data"):
        if is_game_data(candidate):
            return candidate
    matches = [file.parent for file in path.rglob("abuse.lsp") if is_game_data(file.parent)]
    if len(matches) == 1:
        return matches[0]
    raise ValueError("Choose the folder containing abuse.lsp, lisp/, art/, and levels/.")


def download(url, destination, checksum=None):
    parsed = urlsplit(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username is not None:
        raise ValueError("Enter an HTTP or HTTPS link to a game data archive.")
    deadline = time.monotonic() + 30
    digest = hashlib.sha256()
    context = None
    if getattr(sys, "frozen", False):
        import certifi
        context = ssl.create_default_context(cafile=certifi.where())
    with urlopen(url, timeout=8, context=context) as response, destination.open("xb") as output:
        if urlsplit(response.geturl()).scheme not in ("http", "https"):
            raise ValueError("The download redirected outside HTTP or HTTPS.")
        size = 0
        while chunk := response.read1(64 * 1024):
            size += len(chunk)
            if size > MAX_DOWNLOAD:
                raise ValueError("Game data download exceeds 64 MiB.")
            if time.monotonic() > deadline:
                raise ValueError("Game data download timed out.")
            digest.update(chunk)
            output.write(chunk)
    if checksum and digest.hexdigest() != checksum:
        raise ValueError("The upstream archive does not match the published 0.8 checksum.")


def download_upstream(archive):
    try:
        download(UPSTREAM, archive, UPSTREAM_SHA256)
    except (OSError, http.client.HTTPException):
        # The original project advertised HTTP. Verify the same digest on either transport.
        archive.unlink(missing_ok=True)
        download(UPSTREAM.replace("https:", "http:", 1), archive, UPSTREAM_SHA256)


def extract_archive(archive, folder):
    expanded, entries = 0, 0

    def write(name, size, stream):
        nonlocal expanded, entries
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or not path.parts:
            raise ValueError("The archive contains an unsafe file path.")
        expanded += size
        entries += 1
        if expanded > MAX_EXPANDED or entries > MAX_ENTRIES:
            raise ValueError("The unpacked archive exceeds the game data size limit.")
        output = folder.joinpath(*path.parts)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("xb") as file:
            shutil.copyfileobj(stream, file)

    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as files:
            if len(files.infolist()) > MAX_ENTRIES:
                raise ValueError("The ZIP archive contains too many entries.")
            for item in files.infolist():
                kind = stat.S_IFMT(item.external_attr >> 16)
                if kind not in (0, stat.S_IFREG, stat.S_IFDIR):
                    raise ValueError("The archive contains a link or special file.")
                if not item.is_dir():
                    with files.open(item) as stream:
                        write(item.filename, item.file_size, stream)
        return

    with tarfile.open(archive, "r|*") as files:
        for index, item in enumerate(files, 1):
            if index > MAX_ENTRIES:
                raise ValueError("The tar archive contains too many entries.")
            if item.isdir():
                continue
            if not item.isreg():
                raise ValueError("The archive contains a link or special file.")
            with files.extractfile(item) as stream:
                write(item.name, item.size, stream)


def install(source, destination):
    if destination.exists():
        if is_game_data(destination):
            return destination
        raise ValueError(f"{destination} already exists. Move it aside or choose another --data-dir.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".abuse-data-", dir=destination.parent) as work:
        work = Path(work)
        if source == "upstream" or "://" in source:
            archive = work / "download"
            if source == "upstream":
                download_upstream(archive)
            else:
                download(source, archive)
        else:
            archive = Path(source).expanduser().resolve()
            if archive.is_dir():
                root = find_game_data(archive)
                prepared = work / "data"
                shutil.copytree(root, prepared, ignore=shutil.ignore_patterns(".abuse-data-*"))
                prepared.rename(destination)
                return destination
            if not archive.is_file() or archive.stat().st_size > MAX_DOWNLOAD:
                raise ValueError("Choose a game data directory or an archive of at most 64 MiB.")
        extract_archive(archive, work / "extracted")
        find_game_data(work / "extracted").rename(destination)
        return destination


def read_source():
    print("\nAbuse Modern needs the original game data.")
    print("1. Download Abuse 0.8 from abuse.zoy.org")
    print("2. Enter a link to a game data archive")
    print("3. Enter a local archive or extracted game data folder")
    print("4. Quit")
    choice = input("Choose an option [1-4]: ").strip()
    if choice == "1":
        return "upstream"
    if choice == "2":
        url = input("Archive URL: ").strip()
        if not url.startswith(("http://", "https://")):
            raise ValueError("Enter an HTTP or HTTPS link.")
        return url
    if choice == "3":
        location = input("Local path: ").strip().strip('"')
        if not location:
            raise ValueError("Enter the path to the archive or extracted folder.")
        return location
    if choice == "4":
        return None
    raise ValueError("Choose 1, 2, 3, or 4.")


IMPORT_ERRORS = (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile,
                 http.client.HTTPException, RuntimeError, EOFError)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", help="'upstream', an archive URL, or a local archive or data directory")
    parser.add_argument("--data-dir", type=Path, default=data_location(), help="where game data is installed")
    parser.add_argument("--install-only", action="store_true", help="install game data without starting the game")
    parser.add_argument("game_options", nargs=argparse.REMAINDER, help="pass engine options after --")
    args = parser.parse_args()
    data = args.data_dir.expanduser().resolve()
    source = args.source
    while not is_game_data(data):
        try:
            if source is None:
                if not sys.stdin.isatty():
                    raise SystemExit("No game data found. Run interactively or pass --source.")
                try:
                    source = read_source()
                except EOFError:
                    return 1
                if source is None:
                    return 0
            print("Preparing game data…", flush=True)
            install(source, data)
        except KeyboardInterrupt:
            return 1
        except IMPORT_ERRORS as error:
            print(f"Could not install game data. {error}", file=sys.stderr)
            if not sys.stdin.isatty():
                return 1
            source = None
    print(f"Game data ready at {data}", flush=True)
    if args.install_only:
        return 0
    launcher = Path(sys.executable if getattr(sys, "frozen", False) else __file__).resolve()
    engine = launcher.parent / "bin"
    if not getattr(sys, "frozen", False) and not engine.is_dir():
        engine = launcher.parents[1] / "build"
    game = engine / ("abuse.exe" if sys.platform == "win32" else "abuse")
    options = args.game_options
    if options and options[0] == "--":
        options = options[1:]
    environment = os.environ.copy()
    environment.pop("ABUSE_PATH", None)  # Run with the user-selected data directory.
    if getattr(sys, "frozen", False) and sys.platform.startswith("linux"):
        original = environment.get("LD_LIBRARY_PATH_ORIG")
        if original is None:
            environment.pop("LD_LIBRARY_PATH", None)
        else:
            environment["LD_LIBRARY_PATH"] = original
    try:
        return subprocess.call([str(game), "-datadir", str(data), *options], env=environment)
    except OSError as error:
        print(f"Could not start Abuse Modern. {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
