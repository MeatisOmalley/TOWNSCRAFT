# Townscraft64

Nintendo 64 port of Townscraft, the Minecraft-style block game that
originated on the FM TOWNS (`../../fmtowns/townscraft`) and was ported to
the IBM PC as DOScraft (`../../86craft/DOScraft`).

Current stage: **N2 (portable core) done.** N1: pinned libdragon, MIPS GCC
and ares; a hello-triangle ROM boots on 4 MB and 8 MB. N2: a pinned DOScraft
snapshot, its feature checklist, the Towns core compiling for MIPS, and the
player and mob tests passing big-endian in ares. Next: stage DOScraft's own
game source (Towns core plus its overlays) for MIPS.
Nothing here changes the Towns or DOScraft sources. Reports:
[N1](research/n1-toolchain-2026-10-15.md), [N2](research/n2-portable-core-2026-10-15.md).

```
python tools/fetch_dependencies.py                # once: toolchain, libdragon, ares
python tools/build.py                             # hello ROM (needs MSYS2 UCRT64)
powershell -File tools/run_ares.ps1 [-ExpansionPak]

python tools/stage_core.py                        # Towns core + tests -> build/core
python tools/build.py core                        # core objects for MIPS
python tools/run_tests.py --build                 # Towns tests as ROMs in ares
python tools/import_doscraft.py --verify          # vendor/ matches its manifests
```

- [FEATURE_CHECKLIST.md](FEATURE_CHECKLIST.md) — every DOScraft feature,
  tagged core / extract / reimplement / drop. Phase A parity is this list.
- `vendor/doscraft` — read-only DOScraft snapshot at `2ce6b443`, and
  `vendor/towns-tests` — the Towns host tests at `d706a67d`. Regenerate
  only with `tools/import_doscraft.py`.

- [PORT_PLAN.md](PORT_PLAN.md) — target hardware, what carries over from
  the existing ports, what has to be rebuilt for the N64, phases and
  milestones.
- [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) — the planning decisions (Q1–Q7,
  all answered 2026-10-15) and their reasoning.

Like DOScraft, this folder lives in the parent TOWNSCRAFT repository.
Generated builds, ROM images and emulator runtime state belong under the
ignored `build/` directory.
