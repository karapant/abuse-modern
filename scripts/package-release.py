#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a portable Abuse Modern release package from a compiled engine."""

import argparse
import hashlib
from importlib.metadata import files
from pathlib import Path
import re
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import tempfile
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[1]


def bundle_system_python(package, notices):
    """Build a launcher for the current Linux distribution without fetching a bundler."""
    interpreter = Path(sys.executable).resolve()
    stdlib = Path(sysconfig.get_path("stdlib"))
    runtime = package / "runtime"
    (runtime / "bin").mkdir(parents=True)
    shutil.copy2(interpreter, runtime / "bin/python3")
    shutil.copytree(stdlib, runtime / "lib" / stdlib.name,
                    ignore=shutil.ignore_patterns("__pycache__", "site-packages", "dist-packages", "test"))
    shutil.copyfile(stdlib / "LICENSE.txt", notices / "Python.txt")
    # Some distributions build the executable against a separate shared libpython.
    shared_library = sysconfig.get_config_var("INSTSONAME")
    if sysconfig.get_config_var("Py_ENABLE_SHARED") and shared_library:
        libdir = Path(sysconfig.get_config_var("LIBDIR"))
        shutil.copy2(libdir / shared_library, runtime / "lib" / shared_library)
    shutil.copy2(ROOT / "scripts/launch-release.py", package / "launch-release.py")


def install_python_notices(destination):
    for name in ("LICENSE.txt", "LICENSE"):
        license_file = Path(sys.base_prefix) / name
        if license_file.is_file():
            shutil.copy2(license_file, destination / "Python.txt")
            break
    else:
        version = ".".join(map(str, sys.version_info[:3]))
        with urlopen(f"https://raw.githubusercontent.com/python/cpython/v{version}/LICENSE", timeout=15) as source:
            (destination / "Python.txt").write_bytes(source.read())
    for project, expected in (("pyinstaller", "COPYING.txt"), ("certifi", "LICENSE")):
        for item in files(project) or ():
            if item.name == expected:
                shutil.copy2(item.locate(), destination / f"{project}.txt")
                break
        else:
            raise FileNotFoundError(f"Could not find the {project} license in the build environment.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--platform", choices=("linux-x86_64", "macos-arm64", "macos-x86_64"), required=True)
    launcher_options = parser.add_mutually_exclusive_group()
    launcher_options.add_argument("--python-launcher", action="store_true", help="developer package using the system Python")
    launcher_options.add_argument("--bundle-system-python", action="store_true",
                                  help="offline Linux build bundling this distribution's Python")
    args = parser.parse_args()
    if args.platform.startswith("macos-") != (sys.platform == "darwin"):
        parser.error("Build each release package on its target operating system.")
    if args.bundle_system_python and not sys.platform.startswith("linux"):
        parser.error("--bundle-system-python requires Linux.")
    build = args.build.resolve()
    cache = dict(re.findall(r"^([^/#\n:]+):[^=\n]+=(.*)$", (build / "CMakeCache.txt").read_text(), re.M))
    version = cache["CMAKE_PROJECT_VERSION"]
    name = f"abuse-modern-{version}-{args.platform}"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="abuse-release-") as temp:
        temp = Path(temp)
        package = temp / name
        subprocess.run(["cmake", "--install", str(build), "--prefix", str(package)], check=True)
        shutil.copy2(ROOT / "LICENSE", package)
        play = "Play Abuse Modern.command" if sys.platform == "darwin" else "./play"
        (package / "START_HERE.txt").write_text(
            f"Run {play} to install game data and start Abuse Modern.\n"
            "First launch offers the upstream download, an archive URL, or a local archive or directory.\n"
            "The engine and corresponding source are available at https://github.com/karapant/abuse-modern.\n"
        )

        notices = package / "licenses"
        notices.mkdir()
        for dependency in ("SDL3", "SDL3_mixer"):
            checkout = cache.get(f"{dependency}_SOURCE_DIR")
            if not checkout:
                parser.error(f"Package with the bundled {dependency} build to include its license.")
            shutil.copyfile(Path(checkout) / "LICENSE.txt", notices / f"{dependency}.txt")
        shutil.copyfile(Path(cache["SDL3_mixer_SOURCE_DIR"]) / "src/timidity/COPYING",
                        notices / "TiMidity.txt")

        if args.bundle_system_python:
            bundle_system_python(package, notices)
        elif args.python_launcher:
            launcher = package / "abuse-launcher"
            launcher.write_text((ROOT / "scripts/launch-release.py").read_text())
            launcher.chmod(0o755)
        else:
            subprocess.run([sys.executable, "-m", "PyInstaller", "--onefile", "--clean", "--noconfirm",
                            "--name", "abuse-launcher", "--specpath", str(temp), "--workpath", str(temp / "freeze"),
                            "--distpath", str(package), str(ROOT / "scripts/launch-release.py")], check=True)
            install_python_notices(notices)

        launcher_script = """#!/bin/sh
cd "$(dirname "$0")" || exit 1
"""
        if args.bundle_system_python:
            launcher_script += """export PYTHONHOME="$PWD/runtime"
unset PYTHONPATH
export LD_LIBRARY_PATH="$PWD/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./runtime/bin/python3 -S -s ./launch-release.py "$@"
"""
        else:
            launcher_script += 'exec ./abuse-launcher "$@"\n'
        launch = package / play
        launch.write_text(launcher_script)
        launch.chmod(0o755)
        if sys.platform == "darwin":
            binaries = list((package / "bin").iterdir())
            if not args.python_launcher:
                binaries.append(package / "abuse-launcher")
            for binary in binaries:
                subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)
        subprocess.run([str(launch), "--help"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([str(package / "bin/abuse"), "--help"], check=True, stdout=subprocess.DEVNULL)
        if sys.platform == "darwin":
            archive = output / f"{name}.dmg"
            subprocess.run(["hdiutil", "create", "-ov", "-format", "UDZO", "-volname", "Abuse Modern",
                            "-srcfolder", str(package), str(archive)], check=True)
        else:
            archive = output / f"{name}.tar.gz"
            with tarfile.open(archive, "w:gz") as compressed:
                compressed.add(package, arcname=name)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_name(f"{archive.name}.sha256").write_text(f"{digest}  {archive.name}\n")
    print(archive)


if __name__ == "__main__":
    main()
