# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
from io import BytesIO, StringIO
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import URLError
import zipfile

spec = importlib.util.spec_from_file_location(
    "launcher", Path(__file__).resolve().parents[1] / "scripts/launch-release.py"
)
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class ReleaseLauncherTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.destination = self.root / "installed" / "data"
        self.source = self.root / "External Abuse"
        for name in launcher.REQUIRED:
            source_file = self.source / "data" / name
            source_file.parent.mkdir(parents=True, exist_ok=True)
            source_file.write_text(name)

    def make_tar(self):
        archive = self.root / "abuse-0.8.tar.gz"
        with tarfile.open(archive, "w:gz") as files:
            files.add(self.source, "abuse-0.8")
        return archive

    def test_local_directory_and_existing_install(self):
        launcher.install(str(self.source), self.destination)
        self.assertTrue(launcher.is_game_data(self.destination))
        self.assertTrue((self.source / "data/abuse.lsp").is_file())
        self.assertEqual(launcher.install("upstream", self.destination), self.destination)

    def test_wrapped_release_tar(self):
        launcher.install(str(self.make_tar()), self.destination)
        self.assertTrue(launcher.is_game_data(self.destination))

    def test_zip_with_game_data_at_root(self):
        archive = self.root / "game-data.zip"
        with zipfile.ZipFile(archive, "w") as files:
            for name in launcher.REQUIRED:
                files.writestr(name, name)
        launcher.install(str(archive), self.destination)
        self.assertTrue(launcher.is_game_data(self.destination))

    def test_reject_unsafe_zip(self):
        for entry in ("../escape", "folder/../escape", "/escape", "C:/escape"):
            with self.subTest(entry=entry):
                archive = self.root / "unsafe.zip"
                with zipfile.ZipFile(archive, "w") as files:
                    files.writestr(entry, "escape")
                with self.assertRaisesRegex(ValueError, "unsafe"):
                    launcher.install(str(archive), self.destination)
                self.assertFalse(self.destination.exists())

    def test_reject_tar_link(self):
        archive = self.root / "unsafe.tar"
        with tarfile.open(archive, "w") as files:
            member = tarfile.TarInfo("unsafe-link")
            member.type = tarfile.SYMTYPE
            member.linkname = "../escape"
            files.addfile(member)
        with self.assertRaisesRegex(ValueError, "link"):
            launcher.install(str(archive), self.destination)
        self.assertFalse(self.destination.exists())

    def test_invalid_install_is_left_intact(self):
        self.destination.mkdir(parents=True)
        keep = self.destination / "keep"
        keep.write_text("existing")
        with self.assertRaisesRegex(ValueError, "already exists"):
            launcher.install(str(self.source), self.destination)
        self.assertEqual(keep.read_text(), "existing")

    def test_download_sources_and_checksum(self):
        url = "https://example.org/abuse.tgz"
        archive = self.make_tar().read_bytes()

        def response(address, **kwargs):
            result = BytesIO(archive)
            result.geturl = lambda: address
            self.assertEqual(kwargs["timeout"], 8)
            return result

        with patch.object(launcher, "urlopen", side_effect=response):
            with self.assertRaisesRegex(ValueError, "checksum"):
                launcher.install("upstream", self.destination)
            self.assertFalse(self.destination.exists())
            with patch.object(launcher, "UPSTREAM_SHA256", hashlib.sha256(archive).hexdigest()):
                launcher.install("upstream", self.destination)
                launcher.install(url, self.destination.with_name("custom"))
        self.assertTrue(launcher.is_game_data(self.destination))
        self.assertTrue(launcher.is_game_data(self.destination.with_name("custom")))

    def test_unreachable_upstream_can_retry_with_local_data(self):
        arguments = ["play", "--data-dir", str(self.destination), "--install-only"]
        with patch.object(sys, "argv", arguments), patch.object(sys.stdin, "isatty", return_value=True), \
             patch("builtins.input", side_effect=["1", "3", str(self.source)]), \
             patch.object(launcher, "urlopen", side_effect=URLError("timed out")) as request, \
             patch("sys.stdout", new_callable=StringIO), patch("sys.stderr", new_callable=StringIO) as errors:
            self.assertEqual(launcher.main(), 0)
            self.assertIn("timed out", errors.getvalue())
            self.assertEqual(request.call_count, 2)
        self.assertTrue(launcher.is_game_data(self.destination))


if __name__ == "__main__":
    unittest.main()
