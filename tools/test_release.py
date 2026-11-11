from __future__ import annotations

import contextlib
import hashlib
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

import release


class ReleaseChecks(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="slacs-release-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / "package"
        self.output = self.root / "artifacts"
        self.payload = {
            "SKSE/Plugins/SLACS2 NG.dll": b"native plugin",
            "Scripts/SLACS_Main.pex": b"event bridge",
            "SLACS_2_Plus_SCO.esp": b"forms",
            "Textures/slacs2/Oral/head1.dds": b"body texture",
            "Textures/slacs2/clothes/oral1.dds": b"clothing texture",
        }
        for relative, content in self.payload.items():
            destination = self.package / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        self.readme = self.root / "README.md"
        self.readme.write_text("Install both components.", encoding="utf-8")
        self.notes = self.root / "NOTES.md"
        self.notes.write_text("Release notes.", encoding="utf-8")

    def run_release(self, *arguments: str) -> None:
        with patch.multiple(release, PACKAGE=self.package, README=self.readme,
                            RELEASE_NOTES=self.notes), \
                patch.object(release, "TEXTURE_README", self.readme, create=True), \
                patch.object(release, "verify_package"), \
                patch.object(sys, "argv", ["release.py", "--output", str(self.output), *arguments]), \
                contextlib.redirect_stdout(io.StringIO()):
            release.main()

    def archives(self) -> list[Path]:
        return sorted(self.output.glob("*.zip"))

    def test_default_builds_only_main_at_game_data_root(self) -> None:
        self.run_release()
        archives = self.archives()
        self.assertEqual(len(archives), 1)
        self.assertIn("Main", archives[0].stem)
        with zipfile.ZipFile(archives[0]) as bundle:
            names = set(bundle.namelist())
            self.assertIn("SKSE/Plugins/SLACS2 NG.dll", names)
            self.assertIn("Scripts/SLACS_Main.pex", names)
            self.assertIn("SLACS_2_Plus_SCO.esp", names)
            self.assertFalse(any(name.startswith(("Data/", "Textures/")) for name in names))

    def test_both_archives_partition_payload_without_overlapping_files(self) -> None:
        self.run_release("--component", "all")
        self.assertEqual(len(self.archives()), 2)
        seen: set[str] = set()
        delivered: set[str] = set()
        for archive in self.archives():
            with zipfile.ZipFile(archive) as bundle:
                names = set(bundle.namelist())
                self.assertFalse(seen & names)
                seen |= names
                for name in names:
                    self.assertFalse(name.startswith("Data/"))
                    if name in self.payload:
                        self.assertEqual(bundle.read(name), self.payload[name])
                        delivered.add(name)
                if "Textures" in archive.stem:
                    self.assertTrue(any(name.startswith("Textures/") for name in names))
                    self.assertTrue(all(name.startswith(("Textures/", "Docs/")) for name in names))
                else:
                    self.assertFalse(any(name.startswith("Textures/") for name in names))
        self.assertEqual(delivered, set(self.payload))

    def test_later_main_release_preserves_existing_texture_archive(self) -> None:
        self.run_release("--component", "textures")
        texture_archive, = self.archives()
        before = hashlib.sha256(texture_archive.read_bytes()).digest()
        self.run_release()
        self.assertEqual(len(self.archives()), 2)
        self.assertEqual(hashlib.sha256(texture_archive.read_bytes()).digest(), before)


if __name__ == "__main__":
    unittest.main()
