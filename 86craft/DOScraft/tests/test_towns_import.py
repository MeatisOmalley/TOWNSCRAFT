"""Pinned import tests using only unittest, temporary Git repos, and local files."""

import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).absolute().parents[1]
spec = importlib.util.spec_from_file_location("import_towns", ROOT / "tools/import_towns.py")
towns = importlib.util.module_from_spec(spec)
spec.loader.exec_module(towns)


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="test-towns-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.repo = self.base / "repo"
        self.repo.mkdir()
        self.destination = self.base / "vendor/towns"
        self.run_git("init", "--object-format=sha1")
        # Local settings only: tests neither require nor modify global Git config.
        self.run_git("config", "core.autocrlf", "false")
        self.run_git("config", "commit.gpgsign", "false")
        self.run_git("config", "core.hooksPath", str(self.base / "no-hooks"))
        self.original = {
            "src/game.c": b"committed\r\n\x00\xff\n",
            "src/nested/a file.inc": b"nested\n",
            "src/no_extension": b"include every tracked source file",
            "tools/gentables.py": b"print('committed tables')\n",
        }
        for path, data in self.original.items():
            self.write_source(path, data)
        self.write_source("tools/other.py", b"excluded")
        self.write_source("README.md", b"excluded")
        self.run_git("add", "--", towns.SOURCE_ROOT)
        self.run_git("-c", "user.name=Import Test", "-c", "user.email=import@example.invalid",
                     "commit", "--no-verify", "-m", "fixture")
        self.revision = self.run_git("rev-parse", "HEAD").decode().strip()

    def run_git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args],
                              check=True, capture_output=True).stdout

    def write_source(self, path, data):
        target = self.repo / towns.SOURCE_ROOT / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)

    def run_import(self, **kwargs):
        return towns.import_snapshot(self.repo, self.destination,
                                     revision=self.revision, **kwargs)

    def state(self):
        return {path.relative_to(self.destination).as_posix():
                (path.read_bytes(), path.stat().st_mtime_ns)
                for path in self.destination.rglob("*") if path.is_file()}

    def assert_refused_without_changes(self, message):
        before = self.state()
        for verify in (False, True):
            with self.subTest(verify=verify):
                with self.assertRaisesRegex(towns.ImportRefused, message):
                    self.run_import(verify=verify)
                self.assertEqual(self.state(), before)

    def test_pin_and_cli_cannot_override_revision(self):
        self.assertEqual(towns.PINNED_COMMIT, "d706a67db6242627c6233e5593fd1a1e1ddc69af")
        with mock.patch.object(towns, "import_snapshot", return_value=("verified", {"files": [], "source_commit": towns.PINNED_COMMIT})) as importer:
            self.assertEqual(towns.main(["--repo", str(self.repo), "--destination",
                                         str(self.destination), "--verify"]), 0)
            importer.assert_called_once_with(self.repo, self.destination, verify=True)

    def test_committed_bytes_survive_dirty_staged_deleted_and_new_sources(self):
        self.write_source("src/game.c", b"dirty and staged")
        self.run_git("add", "--", f"{towns.SOURCE_ROOT}/src/game.c")
        self.write_source("tools/gentables.py", b"dirty live tables")
        (self.repo / towns.SOURCE_ROOT / "src/nested/a file.inc").unlink()
        self.write_source("src/untracked.c", b"untracked")
        # Advance HEAD as well; the requested old commit remains the source.
        self.run_git("-c", "user.name=Import Test", "-c", "user.email=import@example.invalid",
                     "commit", "--no-verify", "-m", "newer")
        action, manifest = self.run_import()
        self.assertEqual(action, "imported")
        self.assertEqual(manifest["source_commit"], self.revision)
        self.assertEqual(manifest["source_root"], towns.SOURCE_ROOT)
        self.assertEqual([row["path"] for row in manifest["files"]], sorted(self.original))
        for row in manifest["files"]:
            data = self.original[row["path"]]
            self.assertEqual((self.destination / row["path"]).read_bytes(), data)
            self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(row["size"], len(data))
            self.assertEqual(row["source_path"], f"{towns.SOURCE_ROOT}/{row['path']}")
            blob = hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()
            self.assertEqual(row["git_blob"], blob)
            self.assertEqual(row["git_mode"], "100644")
        self.assertEqual(set(self.state()), set(self.original) | {towns.MANIFEST, ".gitattributes"})

    def test_repeat_and_verify_are_no_ops_and_manifest_is_reproducible(self):
        self.run_import()
        before = self.state()
        self.assertEqual(self.run_import()[0], "verified")
        self.assertEqual(self.run_import(verify=True)[0], "verified")
        self.assertEqual(self.state(), before)
        other = self.base / "other/towns"
        towns.import_snapshot(self.repo, other, revision=self.revision)
        for path, (data, _) in before.items():
            self.assertEqual((other / path).read_bytes(), data)
        self.assertEqual(json.loads((other / towns.MANIFEST).read_bytes())["source_commit"], self.revision)

    def test_modified_vendor_is_refused(self):
        self.run_import()
        (self.destination / "src/game.c").write_bytes(b"local edits")
        self.assert_refused_without_changes("modified file: src/game.c")

    def test_missing_vendor_file_is_refused(self):
        self.run_import()
        (self.destination / "src/game.c").unlink()
        self.assert_refused_without_changes("missing file: src/game.c")

    def test_extra_vendor_file_is_refused(self):
        self.run_import()
        (self.destination / "keep.txt").write_bytes(b"user file")
        self.assert_refused_without_changes("unexpected file: keep.txt")

    def test_extra_empty_directory_is_refused(self):
        self.run_import()
        (self.destination / "keep-directory").mkdir()
        self.assert_refused_without_changes("unexpected directory: keep-directory")
        self.assertTrue((self.destination / "keep-directory").is_dir())

    def test_tampered_manifest_is_refused_even_with_matching_local_hash(self):
        self.run_import()
        edited = b"locally changed source"
        (self.destination / "src/game.c").write_bytes(edited)
        path = self.destination / towns.MANIFEST
        manifest = json.loads(path.read_bytes())
        manifest["files"][0]["sha256"] = hashlib.sha256(edited).hexdigest()
        path.write_bytes(json.dumps(manifest).encode())
        self.assert_refused_without_changes("modified file: MANIFEST.json")

    def test_modified_git_attributes_are_refused(self):
        self.run_import()
        (self.destination / ".gitattributes").write_bytes(b"* text\n")
        self.assert_refused_without_changes(r"modified file: \.gitattributes")

    def test_verify_missing_snapshot_never_creates_it(self):
        with self.assertRaisesRegex(towns.ImportRefused, "Snapshot is missing"):
            self.run_import(verify=True)
        self.assertFalse(self.destination.parent.exists())

    def test_empty_or_unmanaged_destination_is_not_adopted(self):
        self.destination.mkdir(parents=True)
        self.assert_refused_without_changes("missing file")

    def test_destination_file_is_not_overwritten(self):
        self.destination.parent.mkdir()
        self.destination.write_bytes(b"keep me")
        with self.assertRaisesRegex(towns.ImportRefused, "not a directory"):
            self.run_import()
        self.assertEqual(self.destination.read_bytes(), b"keep me")

    def test_missing_commit_refuses_before_writes(self):
        with self.assertRaises(towns.ImportRefused):
            towns.import_snapshot(self.repo, self.destination, revision="0" * 40)
        self.assertFalse(self.destination.parent.exists())

    def test_cli_refusal_reports_failure(self):
        with mock.patch.object(towns, "import_snapshot", side_effect=towns.ImportRefused("modified vendor")):
            with mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
                self.assertEqual(towns.main(["--verify"]), 1)
                self.assertIn("Towns import refused: modified vendor", stderr.getvalue())

    def test_executable_git_mode_is_recorded_and_preserved(self):
        self.run_git("update-index", "--chmod=+x", f"{towns.SOURCE_ROOT}/tools/gentables.py")
        self.run_git("-c", "user.name=Import Test", "-c", "user.email=import@example.invalid",
                     "commit", "--no-verify", "-m", "executable generator")
        self.revision = self.run_git("rev-parse", "HEAD").decode().strip()
        _, manifest = self.run_import()
        row = next(row for row in manifest["files"] if row["path"] == "tools/gentables.py")
        self.assertEqual(row["git_mode"], "100755")
        path = self.destination / row["path"]
        if os.name != "nt":
            self.assertTrue(path.stat().st_mode & 0o111)
            path.chmod(0o644)
            self.assert_refused_without_changes("modified executable mode")
        else:
            self.assertEqual(self.run_import(verify=True)[0], "verified")

    def test_destination_parent_link_is_refused(self):
        real = self.base / "real"
        real.mkdir()
        try:
            self.destination.parent.symlink_to(real, target_is_directory=True)
        except OSError as exc:
            self.skipTest(f"Directory symlinks unavailable: {exc}")
        with self.assertRaisesRegex(towns.ImportRefused, "link or reparse point"):
            self.run_import()
        self.assertEqual(list(real.iterdir()), [])

    def test_vendor_link_is_refused(self):
        self.run_import()
        target = self.base / "user-file"
        target.write_bytes(b"keep me")
        try:
            (self.destination / "extra-link").symlink_to(target)
        except OSError as exc:
            self.skipTest(f"File symlinks unavailable: {exc}")
        with self.assertRaisesRegex(towns.ImportRefused, "link or reparse point"):
            self.run_import()
        self.assertEqual(target.read_bytes(), b"keep me")

    def test_git_attributes_disable_byte_transformations(self):
        self.destination = self.repo / "snapshot"
        self.run_import()
        self.run_git("config", "core.autocrlf", "true")
        attributes = self.run_git("check-attr", "text", "filter", "ident",
                                  "working-tree-encoding", "--", "snapshot/src/game.c").decode()
        for name in ("text", "filter", "ident", "working-tree-encoding"):
            self.assertIn(f": {name}: unset", attributes)
        self.run_git("add", "--", "snapshot")
        for path, expected in self.original.items():
            self.assertEqual(self.run_git("show", f":snapshot/{path}"), expected)


class ValidatorTests(unittest.TestCase):
    def test_unsafe_paths(self):
        for path in ("", "/src/x", "../x", "src/../x", "src//x", "src/./x",
                     "src/x\\y", "C:/x", "src/x:", "src/NUL.h", "src/COM1",
                     "src/trailing.", "src/trailing ", "src/control\n", "src/*.c"):
            with self.subTest(path=path), self.assertRaises(towns.ImportRefused):
                towns.validate_path(path)

    def test_portable_paths(self):
        for path in ("src/a.c", "src/nested/a file.inc", "tools/gentables.py"):
            towns.validate_path(path)

    def test_source_links_submodules_and_case_collisions_are_rejected(self):
        for entry in (b"120000 blob " + b"1" * 40 + b"\tfmtowns/townscraft/src/link\0",
                      b"160000 commit " + b"1" * 40 + b"\tfmtowns/townscraft/src/module\0"):
            with self.subTest(entry=entry):
                with mock.patch.object(towns, "git", side_effect=[b"/repo\n", b"commit\n", entry]):
                    with self.assertRaisesRegex(towns.ImportRefused, "Unsupported source entry"):
                        towns.build_snapshot(Path("repo"))
        tree = (b"100644 blob " + b"1" * 40 + b"\tfmtowns/townscraft/src/a.c\0"
                b"100644 blob " + b"2" * 40 + b"\tfmtowns/townscraft/src/A.c\0")
        with mock.patch.object(towns, "git", side_effect=[b"/repo\n", b"commit\n", tree, b"source"]):
            with self.assertRaisesRegex(towns.ImportRefused, "Case-colliding"):
                towns.build_snapshot(Path("repo"))

    def test_incomplete_source_tree_is_rejected(self):
        with mock.patch.object(towns, "git", side_effect=[b"/repo\n", b"commit\n", b""]):
            with self.assertRaisesRegex(towns.ImportRefused, "must contain"):
                towns.build_snapshot(Path("repo"))

    def test_reparse_points_are_detected_without_windows_privileges(self):
        path = mock.Mock()
        path.lstat.return_value = mock.Mock(st_mode=0o040755, st_file_attributes=0x400)
        self.assertTrue(towns.is_link(path))

    def test_non_commit_revision_is_rejected(self):
        with mock.patch.object(towns, "git", side_effect=[b"/repo\n", b"tree\n"]):
            with self.assertRaisesRegex(towns.ImportRefused, "not a commit"):
                towns.build_snapshot(Path("repo"))


if __name__ == "__main__":
    unittest.main()
