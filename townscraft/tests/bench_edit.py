#!/usr/bin/env python3
"""Run a BENCH_EDIT image in Tsugaru_Headless and print g_benchOut.

Build the image first:
  make clean && make EXTRA="-DFIXED_SEED=4242 -DBENCH_EDIT"

Usage:
  bench_edit.py TSUGARU_HEADLESS ROMDIR ISO ELF [--extra "-TOWNSTYPE MODEL2 -FREQ 16 -MEMSIZE 2"]
                [--timeout SEC] [--fd SAVE.BIN]

The emulator runs unthrottled; all times are emulated (VM) time.
"""
import argparse
import re
import subprocess
import sys
import threading
import time

BO = dict(DONE=0, GEN_MS=1, IDLE_FRAMES=2, IDLE_SUM=3, WALK_FRAMES=4, WALK_SUM=5, WALK_MAX=6,
          WALK_OVER66=7, WALK_OVER100=8, WALK_UPD_MAX=9, EDIT=16, EDIT_UPD=32)
NWORDS = 77


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tsugaru")
    ap.add_argument("romdir")
    ap.add_argument("iso")
    ap.add_argument("elf")
    ap.add_argument("--extra", default="-TOWNSTYPE MODEL2 -FREQ 16 -MEMSIZE 2")
    ap.add_argument("--timeout", type=float, default=900)
    ap.add_argument("--prof", help="write g_profSamples dump to this file and symbolize it")
    a = ap.parse_args()

    addr = None
    for line in subprocess.run(["nm", a.elf], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == "g_benchOut":
            addr = int(p[0], 16)
    if addr is None:
        print("g_benchOut not found (not a BENCH_EDIT build?)")
        return 1

    cmd = [a.tsugaru, a.romdir, "-CD", a.iso, "-DONTAUTOSAVECMOS", "-NOWAITBOOT"] + a.extra.split()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, bufsize=1)
    lines = []
    lock = threading.Lock()

    def reader():
        for l in p.stdout:
            with lock:
                lines.append(l)
    th = threading.Thread(target=reader, daemon=True)
    th.start()

    def dump():
        with lock:
            del lines[:]
        p.stdin.write("!MD PHYS:%X 16 %d 1 0\n" % (addr, (NWORDS * 4 + 15) // 16))
        p.stdin.flush()
        time.sleep(1.0)
        data = bytearray()
        with lock:
            ls = list(lines)
        for l in ls:
            m = re.match(r"^([0-9A-F]{8})\s+((?:[0-9A-F]{2}\s?){16})", l)
            if m:
                data += bytes(int(b, 16) for b in m.group(2).split())
        if len(data) < NWORDS * 4:
            return None
        return [int.from_bytes(data[i * 4:i * 4 + 4], "little") for i in range(NWORDS)]

    t0 = time.time()
    out = None
    while time.time() - t0 < a.timeout:
        time.sleep(4)
        v = dump()
        if v and v[0] == 1:
            out = v
            break
    sec = None
    if out:
        saddr = None
        for line in subprocess.run(["nm", a.elf], capture_output=True, text=True).stdout.splitlines():
            q = line.split()
            if len(q) == 3 and q[2] == "g_benchSec":
                saddr = int(q[0], 16)
        if saddr:
            with lock:
                del lines[:]
            p.stdin.write("!MD PHYS:%X 16 12 1 0\n" % saddr)
            p.stdin.flush()
            time.sleep(1.0)
            data = bytearray()
            with lock:
                ls = list(lines)
            for l in ls:
                m = re.match(r"^([0-9A-F]{8})\s+((?:[0-9A-F]{2}\s?){16})", l)
                if m:
                    data += bytes(int(b, 16) for b in m.group(2).split())
            if len(data) >= 192:
                sec = [int.from_bytes(data[i * 4:i * 4 + 4], "little") for i in range(48)]
    if out and a.prof:
        paddr = None
        for line in subprocess.run(["nm", a.elf], capture_output=True, text=True).stdout.splitlines():
            q = line.split()
            if len(q) == 3 and q[2] == "g_profSamples":
                paddr = int(q[0], 16)
        with lock:
            del lines[:]
        p.stdin.write("!MD PHYS:%X 16 1024\n" % paddr)
        p.stdin.flush()
        time.sleep(3)
        with lock:
            open(a.prof, "w").write("".join(lines))
    p.stdin.write("!Q\n")
    p.stdin.flush()
    try:
        p.wait(timeout=10)
    except Exception:
        p.kill()
    if not out:
        print("TIMEOUT (last dump: %s)" % (v,))
        return 1

    edits = out[16:32]
    upd = out[32:48]
    idle = out[3] / max(1, out[2])
    walk = out[5] / max(1, out[4])
    print("gen+initial meshes : %8.2f s" % (out[1] / 1000.0))
    print("idle frame avg     : %8.1f ms  (%.1f fps)" % (idle / 1000, 1e6 / max(1, idle)))
    print("edit frame max     : %s ms" % " ".join("%.0f" % (e / 1000) for e in edits))
    print("edit update        : %s ms" % " ".join("%.0f" % (e / 1000) for e in upd))
    print("edit collect       : %s ms" % " ".join("%.0f" % (e / 1000) for e in out[48:64]))
    print("edit worst / mean  : %8.1f / %.1f ms" % (max(edits) / 1000, sum(edits) / len(edits) / 1000))
    print("walk frame avg     : %8.1f ms  (%.1f fps), max %.0f ms, >66ms: %d, >100ms: %d, upd max %.0f ms"
          % (walk / 1000, 1e6 / max(1, walk), out[6] / 1000, out[7], out[8], out[9] / 1000))
    pc = out[64:76]
    for name, k in (("start", 0), ("edits", 1), ("walk", 2)):
        d = [pc[k * 4 + i] - (pc[(k - 1) * 4 + i] if k else 0) for i in range(4)]
        print("%-6s pool compactions %d, evictions %d, rebuilds %d (%d layers)" % ((name,) + tuple(d)))
    look = out[15] / max(1, out[14])
    print("look frame avg     : %8.1f ms  (%.1f fps)" % (look / 1000, 1e6 / max(1, look)))
    if sec:
        names = ["game", "update", "collect", "entsort", "wait", "sky", "draw", "finish", "hud", "present"]
        for ph, row in (("look", sec[0:24]), ("walk", sec[24:48])):
            fr = max(1, row[10])
            tot = sum(row[0:10])
            print("%s: %d frames, %.1f ms/frame: " % (ph, row[10], tot / fr / 1000) +
                  " ".join("%s %.1f" % (n, row[i] / fr / 1000) for i, n in enumerate(names)))
            print("      per frame: %d texels (%.2fx of 160x100), %d polys, %d items" %
                  (row[11] / fr, row[11] / fr / 16000.0, row[12] / fr, row[13] / fr))
            print("      per frame: %.1f quads (incl. split cells), %.1f models, %.1f mob boxes; %.1f rejected early; polys: %.1f off screen, %.1f tiny, %.1f near clipped, %.1f guard clipped" %
                  tuple(row[i] / fr for i in (17, 18, 19, 21, 14, 15, 16, 20)))
    if a.prof:
        import os
        print(subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "profile.py"),
                              a.elf, a.prof], capture_output=True, text=True).stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
