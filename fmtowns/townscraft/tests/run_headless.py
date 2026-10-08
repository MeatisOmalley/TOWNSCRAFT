#!/usr/bin/env python3
"""Run TOWNSCRAFT in Tsugaru_Headless with the test-only stub ROM.

Usage:
  run_headless.py TSUGARU_HEADLESS ROMDIR ISO OUTDIR [--evt events.txt]
                  [--shots t1,t2,...] [--quit T] [--freq MHZ]

Screenshots are taken at the given wall-clock seconds after start and saved as
OUTDIR/shot_<t>.png.  Emulator console output is written to OUTDIR/log.txt.
"""
import argparse
import os
import subprocess
import sys
import threading
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tsugaru")
    ap.add_argument("romdir")
    ap.add_argument("iso")
    ap.add_argument("outdir")
    ap.add_argument("--evt")
    ap.add_argument("--shots", default="5")
    ap.add_argument("--quit", type=float, default=None)
    ap.add_argument("--freq", default=None)
    ap.add_argument("--extra", default="")
    ap.add_argument("--cmd", action="append", default=[], help="T:COMMAND console command at wall-clock T")
    a = ap.parse_args()

    os.makedirs(a.outdir, exist_ok=True)
    cmd = [a.tsugaru, a.romdir, "-CD", a.iso, "-DONTAUTOSAVECMOS", "-NOWAITBOOT"]
    if a.freq:
        cmd += ["-FREQ", a.freq]
    if a.evt:
        cmd += ["-EVTLOG", a.evt]
    cmd += a.extra.split()
    log = open(os.path.join(a.outdir, "log.txt"), "w")
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, text=True)

    shots = [float(t) for t in a.shots.split(",") if t]
    quitAt = a.quit if a.quit is not None else (max(shots) + 1 if shots else 5)
    t0 = time.time()
    events = [(t, "!SS " + os.path.abspath(os.path.join(a.outdir, "shot_%g.png" % t))) for t in shots]
    for c in a.cmd:
        t, cc = c.split(":", 1)
        events.append((float(t), "!" + cc))
    events.append((quitAt, "!Q"))
    events.sort()
    try:
        for t, c in events:
            dt = t - (time.time() - t0)
            if dt > 0:
                time.sleep(dt)
            p.stdin.write(c + "\n")
            p.stdin.flush()
        p.wait(timeout=20)
    except Exception as e:
        print("Exception", e)
        p.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
