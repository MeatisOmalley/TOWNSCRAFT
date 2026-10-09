"""Controlled Towns house investigation, not a PC port or production build.

Uses the existing snapshot compiler and guest-clock measurement. Only writes
ignored DOScraft/build artifacts. Never changes Towns sources or launch images.
Run from any directory: python 86craft/DOScraft/tools/audit_towns_house.py
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOWNS = ROOT.parents[1] / "fmtowns/townscraft"


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--ram", type=int, choices=(2, 8), default=8)
    cli.add_argument("--timeout", type=int, default=240)
    args = cli.parse_args()
    spec = importlib.util.spec_from_file_location("towns_audit", TOWNS / "tools/perf_audit.py")
    perf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(perf)
    (ROOT / "build").mkdir(exist_ok=True)
    shadow = Path(tempfile.mkdtemp(prefix="house-fixture-input-", dir=ROOT / "build"))
    for name in ("src", "boot", "tools"):
        shutil.copytree(TOWNS / name, shadow / name, dirs_exist_ok=True)
    shutil.copy2(TOWNS / "Makefile", shadow / "Makefile")
    shutil.copytree(TOWNS / "tests", shadow / "tests", dirs_exist_ok=True)
    level = (TOWNS / "tests/standard_level.inc").read_text()
    marker = "\treturn b;"
    assert level.count(marker) == 1
    # Same background in both cases. A 17x13, six-block-high complete shell.
    house = """
#if HOUSE_PRESENT
    if(x>=32 && x<=48 && z>=43 && z<=55 && y>=24 && y<=30)
    {
        if(x==32 || x==48 || z==43 || z==55 || y==30) b=B_STONE;
        if(z==43 && x>=39 && x<=40 && y<=26) b=B_AIR; /* doorway */
        if(z==55 && x>=39 && x<=41 && y>=26 && y<=28) b=B_GLASS;
    }
#endif
"""
    (shadow / "tests/standard_level.inc").write_text(level.replace(marker, house + marker))
    (shadow / "tests/standard_probe.inc").write_text((ROOT / "tests/house_probe.inc").read_text())
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=TOWNS, text=True).strip()
    source_hash = hashlib.sha256()
    for path in sorted((TOWNS / "src").iterdir()):
        if path.is_file():
            source_hash.update(path.name.encode()); source_hash.update(path.read_bytes())
    summaries = []
    for present in (0, 1):
        for occ in (0, 1):
            name = f"house-{present}-occ-{occ}-ram-{args.ram}"
            out = ROOT / "build" / name
            out.mkdir(parents=True, exist_ok=True)
            options = argparse.Namespace(
                ref="WORKTREE", production=False, standard=True, mesh_probe=False,
                hdd_probe=False, width=80, scale=2, view=12, mobs=0, phase=-1,
                linear_test=False, sync_mesh=False, full_mesh=False,
                define=[f"HOUSE_PRESENT={present}", f"HOUSE_OCC={occ}"],
                zig=str(TOWNS / "build/tools/zig-windows-x86_64-0.13.0/zig.exe"),
                ram=args.ram, freq=16, towns1989=False, hdd=None,
                timeout=args.timeout, name=name,
            )
            perf.ROOT = shadow
            perf.build(options, out)
            perf.ROOT = TOWNS
            perf.measure(options, out)
            result = json.loads((out / "result.json").read_text())
            labels = ("outside_static", "inside_static", "inside_moving", "inside_up")
            old_labels = ("look", "walk", "down", "up")
            result["phases"] = {label: result["phases"][old] for label, old in zip(labels, old_labels)}
            for row in result["phases"].values():
                row["texture_span_samples_per_frame"] = row.pop("edits") / row["frames"]
            result.update(house=bool(present), occlusion=bool(occ), source_revision=revision,
                          source_sha256=source_hash.hexdigest(),
                          limitations="Synthetic shell, frozen simulation, no HDD, 12-block view; not the user's actual house or PC FPS.")
            (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            summaries.append(result)
            print("FINISHED " + name, flush=True)
    (ROOT / "build" / f"house-results-ram-{args.ram}.json").write_text(json.dumps(summaries, indent=2) + "\n")


if __name__ == "__main__":
    main()
