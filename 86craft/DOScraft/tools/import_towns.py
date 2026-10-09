"""Import the pinned Towns Git blobs, or verify an existing import.

Run with Python 3: tools/import_towns.py [--verify]. No source worktree files
are read. An existing vendor directory must match exactly; it is never repaired,
updated, or deleted. The manifest is deterministic and has no timestamps or
machine-specific paths. No compiler, third-party Python package, or network is
needed, only Git and the pinned commit in the parent repository.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tempfile


ROOT = Path(__file__).absolute().parents[1]
PINNED_COMMIT = "d706a67db6242627c6233e5593fd1a1e1ddc69af"
SOURCE_ROOT = "fmtowns/townscraft"
MANIFEST = "MANIFEST.json"
# Suppress checkout/add transformations even when the parent uses autocrlf.
ATTRIBUTES = b"* -text -filter -ident -working-tree-encoding\n"


class ImportRefused(RuntimeError):
    """The source or destination cannot safely satisfy the pinned import."""


def git(repo, *args):
    try:
        result = subprocess.run(
            ["git", "-C", str(repo), *args], capture_output=True, check=True
        )
    except FileNotFoundError as exc:
        raise ImportRefused("Git is required and was not found") from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.decode("utf-8", errors="replace").strip()
        raise ImportRefused(f"Git {' '.join(args)} failed: {detail}") from exc
    return result.stdout


def validate_path(path):
    """Require a portable, literal relative path; never normalize unsafe names."""
    reserved = {"CON", "PRN", "AUX", "NUL"}
    reserved.update(f"{prefix}{n}" for prefix in ("COM", "LPT") for n in range(1, 10))
    parts = path.split("/")
    if not path or PurePosixPath(path).is_absolute():
        raise ImportRefused(f"Unsafe source path: {path!r}")
    for part in parts:
        if (not part or part in (".", "..") or part.endswith((".", " "))
                or any(ord(c) < 32 or c in '\\:<>"|?*' for c in part)
                or part.split(".")[0].upper() in reserved):
            raise ImportRefused(f"Unsafe source path: {path!r}")


def build_snapshot(repo, revision=PINNED_COMMIT):
    """Read only committed objects. The CLI always uses PINNED_COMMIT.

    The revision parameter lets standard-library tests use isolated Git repos.
    Return the exact output bytes and manifest, before any destination writes.
    """
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ImportRefused("Source revision must be a full lowercase commit ID")
    repo = Path(os.fsdecode(git(repo, "rev-parse", "--show-toplevel").rstrip(b"\r\n")))
    if git(repo, "cat-file", "-t", revision).strip() != b"commit":
        raise ImportRefused(f"Pinned source {revision} is not a commit")
    tree = git(repo, "ls-tree", "-r", "-z", "--full-tree", revision, "--",
               f"{SOURCE_ROOT}/src", f"{SOURCE_ROOT}/tools/gentables.py")
    records = []
    seen = set()
    files = {}
    for entry in tree.split(b"\0"):
        if not entry:
            continue
        try:
            header, raw_path = entry.split(b"\t", 1)
            mode, kind, blob = header.decode("ascii").split()
            source_path = raw_path.decode("utf-8")
        except (UnicodeError, ValueError) as exc:
            raise ImportRefused("Malformed or non-UTF-8 Git tree entry") from exc
        if kind != "blob" or mode not in ("100644", "100755"):
            raise ImportRefused(f"Unsupported source entry: {source_path} ({mode} {kind})")
        if not (source_path.startswith(f"{SOURCE_ROOT}/src/")
                or source_path == f"{SOURCE_ROOT}/tools/gentables.py"):
            raise ImportRefused(f"Unexpected source entry: {source_path}")
        path = source_path[len(SOURCE_ROOT) + 1:]
        validate_path(path)
        if path.casefold() in seen:
            raise ImportRefused(f"Case-colliding source path: {path}")
        seen.add(path.casefold())
        data = git(repo, "cat-file", "blob", blob)
        files[path] = data
        records.append({"path": path, "source_path": source_path,
                        "git_blob": blob, "git_mode": mode,
                        "sha256": hashlib.sha256(data).hexdigest(),
                        "size": len(data)})
    if "tools/gentables.py" not in files or not any(p.startswith("src/") for p in files):
        raise ImportRefused("Pinned commit must contain src files and tools/gentables.py")
    manifest = {"schema_version": 1, "source_commit": revision,
                "source_root": SOURCE_ROOT,
                "files": sorted(records, key=lambda record: record["path"])}
    files[MANIFEST] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    files[".gitattributes"] = ATTRIBUTES
    return files, manifest


def is_link(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    )


def check_destination_path(destination):
    # Do not resolve away links/junctions before checking them.
    destination = Path(os.path.abspath(destination))
    for path in reversed((destination, *destination.parents)):
        if os.path.lexists(path):
            if is_link(path):
                raise ImportRefused(f"Destination uses a link or reparse point: {path}")
            if not path.is_dir():
                raise ImportRefused(f"Destination path is not a directory: {path}")
    return destination


def verify_snapshot(destination, files, manifest):
    """Compare exact paths and bytes with Git, including the manifest itself."""
    destination = check_destination_path(destination)
    if not destination.is_dir():
        raise ImportRefused(f"Snapshot is missing: {destination}")
    actual_files = set()
    actual_dirs = set()

    def visit(directory):
        for path in directory.iterdir():
            relative = path.relative_to(destination).as_posix()
            if is_link(path):
                raise ImportRefused(f"Snapshot contains a link or reparse point: {relative}")
            if path.is_dir():
                actual_dirs.add(relative)
                visit(path)
            elif path.is_file():
                actual_files.add(relative)
            else:
                raise ImportRefused(f"Snapshot contains a non-regular file: {relative}")

    visit(destination)
    expected_dirs = {str(parent) for name in files
                     for parent in PurePosixPath(name).parents if str(parent) != "."}
    problems = []
    for name in sorted(set(files) - actual_files):
        problems.append(f"missing file: {name}")
    for name in sorted(actual_files - set(files)):
        problems.append(f"unexpected file: {name}")
    for name in sorted(actual_dirs - expected_dirs):
        problems.append(f"unexpected directory: {name}")
    for name in sorted(set(files) & actual_files):
        if (destination / name).read_bytes() != files[name]:
            problems.append(f"modified file: {name}")
    if os.name != "nt":
        for record in manifest["files"]:
            path = destination / record["path"]
            if record["path"] in actual_files:
                executable = bool(path.stat().st_mode & 0o111)
                if executable != (record["git_mode"] == "100755"):
                    problems.append(f"modified executable mode: {record['path']}")
    if problems:
        raise ImportRefused("Existing snapshot differs; refusing to overwrite or delete:\n  "
                            + "\n  ".join(problems))


def import_snapshot(repo, destination, *, verify=False, revision=PINNED_COMMIT):
    files, manifest = build_snapshot(repo, revision)
    destination = check_destination_path(destination)
    if verify or destination.exists():
        verify_snapshot(destination, files, manifest)
        return "verified", manifest
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Stage only in a uniquely allocated temporary directory. Cleanup is confined
    # to that directory; existing vendor content is never removed or overwritten.
    with tempfile.TemporaryDirectory(prefix=".towns-import-", dir=destination.parent) as temporary:
        staged = Path(temporary) / "towns"
        staged.mkdir()
        modes = {row["path"]: row["git_mode"] for row in manifest["files"]}
        for name, data in files.items():
            path = staged / name
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("xb") as output:
                output.write(data)
            if os.name != "nt":
                path.chmod(0o755 if modes.get(name) == "100755" else 0o644)
        verify_snapshot(staged, files, manifest)
        check_destination_path(destination)
        if destination.exists():
            raise ImportRefused(f"Destination appeared during import; refusing to replace: {destination}")
        staged.rename(destination)
    return "imported", manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=ROOT,
                        help="Parent Git repository containing the pinned commit")
    parser.add_argument("--destination", type=Path, default=ROOT / "vendor/towns",
                        help="Snapshot directory (default: DOScraft/vendor/towns)")
    parser.add_argument("--verify", action="store_true",
                        help="Verify exact manifest, paths and bytes without writing")
    args = parser.parse_args(argv)
    try:
        action, manifest = import_snapshot(args.repo, args.destination, verify=args.verify)
    except (ImportRefused, OSError) as exc:
        print(f"Towns import refused: {exc}", file=sys.stderr)
        return 1
    print(f"Towns {action}: {len(manifest['files'])} files from {manifest['source_commit']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
