#!/usr/bin/env python3
"""Screenshots of fixed camera poses from a BENCH_SHOTS build, for checking
that renderer changes leave the image unchanged.

  make EXTRA="-DFIXED_SEED=4242 -DBENCH_EDIT -DBENCH_SHOTS"
  bench_shots.py TSUGARU_HEADLESS ROMDIR ISO ELF OUTDIR [--extra ...]
  bench_shots.py --compare DIR_A DIR_B      (pixel differences per pose)
"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time


def compare(a, b):
    from PIL import Image, ImageChops
    worst = 0
    for f in sorted(os.listdir(a)):
        if not f.endswith(".png"):
            continue
        pa, pb = os.path.join(a, f), os.path.join(b, f)
        if not os.path.exists(pb):
            print("%s: missing in %s" % (f, b))
            worst = max(worst, 1)
            continue
        ia, ib = Image.open(pa).convert("RGB"), Image.open(pb).convert("RGB")
        if ia.size != ib.size:
            print("%s: size %s vs %s" % (f, ia.size, ib.size))
            worst = max(worst, 1)
            continue
        d = ImageChops.difference(ia, ib).convert("L")
        n = sum(1 for p in d.getdata() if p)
        print("%s: %d pixels differ (%.2f%%)" % (f, n, 100.0 * n / (ia.size[0] * ia.size[1])))
        worst = max(worst, n)
    return 0 if 0 == worst else 2


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--compare":
        return compare(sys.argv[2], sys.argv[3])
    ap = argparse.ArgumentParser()
    ap.add_argument("tsugaru")
    ap.add_argument("romdir")
    ap.add_argument("iso")
    ap.add_argument("elf")
    ap.add_argument("outdir")
    ap.add_argument("--extra", default="-TOWNSTYPE MODEL2 -FREQ 16 -MEMSIZE 2")
    ap.add_argument("--timeout", type=float, default=120)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    addr = None
    for line in subprocess.run(["nm", a.elf], capture_output=True, text=True).stdout.splitlines():
        q = line.split()
        if len(q) == 3 and q[2] == "g_benchOut":
            addr = int(q[0], 16)
    cmd = [a.tsugaru, a.romdir, "-CD", a.iso, "-DONTAUTOSAVECMOS", "-NOWAITBOOT"] + a.extra.split()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, bufsize=1)
    lines = []
    lock = threading.Lock()

    def reader():
        for l in p.stdout:
            with lock:
                lines.append(l)
    threading.Thread(target=reader, daemon=True).start()

    def words():
        with lock:
            del lines[:]
        p.stdin.write("!MD PHYS:%X 16 20 1 0\n" % addr)
        p.stdin.flush()
        time.sleep(0.2)
        data = bytearray()
        with lock:
            ls = list(lines)
        for l in ls:
            m = re.match(r"^([0-9A-F]{8})\s+((?:[0-9A-F]{2}\s?){16})", l)
            if m:
                data += bytes(int(b, 16) for b in m.group(2).split())
        if len(data) < 320:
            return None
        return [int.from_bytes(data[i * 4:i * 4 + 4], "little") for i in range(80)]

    taken = set()
    t0 = time.time()
    while time.time() - t0 < a.timeout:
        w = words()
        if not w:
            continue
        if w[0] == 1:
            break
        k = w[76]
        if k and k not in taken:
            p.stdin.write("!SS %s\n" % os.path.abspath(os.path.join(a.outdir, "pose_%02d.png" % k)))
            p.stdin.flush()
            taken.add(k)
    p.stdin.write("!Q\n")
    p.stdin.flush()
    try:
        p.wait(timeout=10)
    except Exception:
        p.kill()
    print("%d screenshots in %s" % (len(taken), a.outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
