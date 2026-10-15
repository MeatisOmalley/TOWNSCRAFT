# Townscraft64

Nintendo 64 port of Townscraft, the Minecraft-style block game that
originated on the FM TOWNS (`../../fmtowns/townscraft`) and was ported to
the IBM PC as DOScraft (`../../86craft/DOScraft`).

Current stage: **N1 (toolchain) done.** Pinned libdragon, MIPS GCC and ares;
a hello-triangle ROM boots in ares on 4 MB and 8 MB. No game code yet.
Nothing here changes the Towns or DOScraft sources. See
[the N1 report](research/n1-toolchain-2026-10-15.md).

```
python tools/fetch_dependencies.py                # once
python tools/build.py                             # needs MSYS2 UCRT64
powershell -File tools/run_ares.ps1 [-ExpansionPak]
```

- [PORT_PLAN.md](PORT_PLAN.md) — target hardware, what carries over from
  the existing ports, what has to be rebuilt for the N64, phases and
  milestones.
- [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) — the planning decisions (Q1–Q7,
  all answered 2026-10-15) and their reasoning.

Like DOScraft, this folder lives in the parent TOWNSCRAFT repository.
Generated builds, ROM images and emulator runtime state belong under the
ignored `build/` directory.
