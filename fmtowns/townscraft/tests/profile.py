#!/usr/bin/env python3
"""Symbolize EIP samples dumped from the VM (g_profSamples) using nm."""
import re
import subprocess
import sys
from collections import Counter

elf, log = sys.argv[1], sys.argv[2]
syms = []
for line in subprocess.run(["nm", "-n", elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in "tT":
        syms.append((int(p[0], 16), p[2]))
data = bytearray()
for line in open(log, errors="replace"):
    m = re.match(r"^([0-9A-F]{8}) ([0-9A-F ]{47})\|", line)
    if m:
        data += bytes(int(b, 16) for b in m.group(2).split())
samples = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data) - 3, 4)]
samples = [s for s in samples if s]
c = Counter()
for s in samples:
    name = "?"
    for a, n in syms:
        if a <= s:
            name = n
        else:
            break
    c[name] += 1
tot = sum(c.values())
for n, k in c.most_common(25):
    print("%6.1f%% %5d %s" % (100.0 * k / tot, k, n))

if len(sys.argv) > 3:
    # Line-level breakdown for one function
    fn = sys.argv[3]
    lo = [a for a, n in syms if n == fn][0]
    hi = min([a for a, n in syms if a > lo] + [1 << 32])
    inside = [s for s in samples if lo <= s < hi]
    addrs = Counter(inside)
    out = subprocess.run(["addr2line", "-e", elf] + ["%x" % a for a in addrs], capture_output=True, text=True).stdout.split()
    lc = Counter()
    for (a, k), l in zip(addrs.items(), out):
        lc[l.split("/")[-1]] += k
    print("--- %s" % fn)
    for l, k in lc.most_common(20):
        print("%5d %s" % (k, l))
