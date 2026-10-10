# DOScraft: Townscraft-to-IBM-PC port plan

Research and investigation: 2026-10-09. This is the architecture/measurement phase, not a working DOS game. No production Towns gameplay, UI, renderer or emulator behavior was changed for this plan.

Direct-port checkpoint, 2026-10-09: DOS startup backing and resident IRQ0/IRQ1 handlers now pass the combined `SYSTEM.EXE` guest diagnostic. Installed RAM remains 16 MiB; separate 1-MiB low / 8-MiB high arenas preserve permanent-versus-rewind lifetime boundaries without altering world/cache budgets. The timer remains 100 Hz beneath the unchanged 20 Hz simulation; BIOS clock chaining, AT controller key delivery, nested interrupt guards, sampling profiler, shutdown/reinstallation and Mode X presentation with IRQs active passed. Sampling frame decoding supports the pinned normal CWSDPMI r7 only. `MOUSE.EXE` now passes serial UART loopback/IRQ4/INT33 transport, signed deltas, buttons, 16-bit accumulation and reset parity with pinned CuteMouse 1.9.1 in linear mode. Physical/Windows keyboard and mouse ingress, SB/OPL audio, world/save storage integration and the complete game link still need verification. No Phase B optimization is included.

## Recommendation

Separately approved bugfix checkpoint: completed DOS saved-column reads release
their destination; repeated arguments now recopy instead of accepting stale
bytes left by terrain reads. Single-/multi-track native regressions and the
guest `REUSED_DESTINATION` check pass, with unchanged save-file bytes. The other
approved fix, legacy/v4 precedence, uses a loader-only track-zero header probe in
its own commit. Recognized legacy v1-v3 beats a surviving v4 bank 1, with original
payload validation and errors; no bank is discarded or format rewritten. Legacy
has no generation, so cross-format chronology is explicitly unknowable. Vendor remains
immutable; real mixed-source gameplay parity remains pending.

The legacy-precedence correction is committed separately (`27ab11cf`). Its
extended guest fixture first passed with a private CRT initial-memory-lock
experiment; the default unlocked build exposed a startup interrupt-return
fault before the fixture began. The normal IRQ adapter now requests resident,
non-moving CRT memory, with the checked ISR-image lock retained. Normal builds
pass the extended save fixture/whole-file oracle and all startup/IRQ lifecycle,
heap, profiler and VGA gates. This separate checkpoint is port infrastructure,
not Phase B work. Emulator/ROM/libc binaries are unchanged.

Temporary terrain transport checkpoint, 2026-10-09: `hdd_*` now maps to an existing dedicated DOS scratch file without creation/formatting. Native failure/ownership tests and `STORAGE.EXE` pass, including exact host verification of changed slots, bounds, cancellation and checked flush/reopen. DOS I/O remains synchronous even with 2-KiB polling slices; no traversal-hitch or speedup claim. The unchanged `column_hdd.inc` owns the RAM-only directory, alternating slots and edit revisions; world integration is not yet verified. Preserve the pinned version selection: the 16-MiB, width-256 baseline uses legacy v3, while the existing v4 two-bank format requires width below 256. Do not force v4 or introduce a new transactional envelope during the direct port without separately approving that storage-policy adaptation.

Persistent transport/codec checkpoint, 2026-10-09: `C:\WORLD.SAV` is a separate existing raw Towns logical medium, preserving exact capacity, codecs, checksums, save versions and UI wording. Bounded DOS close-error adaptations and the separately approved loader-precedence fix are recorded in the provenance-checked codec extraction. `SAVEIO.EXE` passes primary-guest v3/v4 fixture roundtrips, source-bank protection and paging with resident IRQs; native faults cover v1/v2, short/zero/error I/O, all commit boundaries and close failures. Both complete diagnostic save files match an independent byte oracle. World/mob providers are mocks: real mixed-source ownership, mob serialization, edits, dirty eviction, cold restart and gameplay parity are still next, alongside remaining audio/game linking. Separately approved inherited save/cache fixes are documented in [KNOWN_PORT_ISSUES.md](KNOWN_PORT_ISSUES.md). No Phase B work is included.

Actual-state integration checkpoint: the pinned world/cache, mob manager/codec and inventory now run together with DOS heap/save/terrain transports in a deterministic width-256 fixture. Native tests pass dirty eviction, block/torch persistence and relighting, exact resident terrain, active/dormant identity/state, player/inventory/chests, legacy v3 roundtrip and fresh-process reload. The entire save matches an independent Python byte oracle. `WORLDIO.EXE` prepares both files on fresh private media and runs `/WRITE` then `/RELOAD` in separate DOS processes. This does not change the source pin, spawning/freeze rules, generation algorithms, save-version selection or allocation budgets. Unused rendering/audio/combat/physics callbacks remain diagnostic stubs, and no full game or performance claim follows. Real v4 mixed-source ownership, procedural-generation/gameplay, audio and complete linking remain pending. The user explicitly deferred the passive-spawn timing change until after port work.

Both primary-guest WORLDIO phases passed on 2026-10-09, with no terrain command failures. The complete guest save remained byte-identical to the independent reference after fresh-process reload. [Reports, hashes and limitations](tests/baselines/2026-10-09-world-storage.json). The completed private VM was closed; source/vendor policy and user media were not changed.

Latest resolution guidance, 2026-10-09: the user authorizes selecting the resolution that makes sense for the target PC. References to 320x240 below describe the direct-port starting layout, not an immutable requirement. First candidate is Mode X presentation with the existing sharp HUD and doubled 160x100 3D option. Resolve the final resolution using playable-port measurements; unrelated UI/gameplay redesign remains out of scope.

Build a real 32-bit DOS executable for the **486DX/25 primary target**, with 16 MB RAM, ISA ET4000AX 1 MB, the agreed ESDI disk and Sound Blaster 1.x. The Everex-class 386DX/33 is a deferred compatibility/port project. Keep 86Box as the hardware emulator, not the game engine. **Phase A is a fairly direct, complete port of the existing game; Phase B is separate optimization work and begins only after the user accepts full baseline parity.** A playable vertical slice alone does not open Phase B.

For Phase B, the leading research group is **correct opaque front-to-back traversal, uncovered-span coverage and hierarchical screen-tile rejection**, with depth information for cutouts and moving models. These form one architectural investigation, tested incrementally. A depth reference is allowed in Phase A's correctness harness; replacing the production painter renderer is Phase B. Neither architecture nor an FPS gain is promised. See [OPTIMIZATION_BACKLOG.md](OPTIMIZATION_BACKLOG.md) for the attachment-derived priorities and inherited-method audit.

The first playable milestone is a small deterministic world with walking, full up/down looking, block placement/removal, inventory and unchanged controls. It is an intermediate Phase A checkpoint. All existing gameplay, features, audio, menus and storage semantics must follow before parity acceptance; renderer competition and large-world redesign must wait.

### Phase A scope and acceptance boundary

Pin and inventory the source baseline, then preserve its gameplay and existing optimization methods while adapting only the platform dependencies needed for DOS. The parity inventory must cover every existing feature, including controls, 320x240 game UI (200-row view plus 40-row HUD), menus and graphics options; blocks/items/textures and cutout rules; crafting, furnaces and containers; collision, full camera pitch, building and edit visibility; generation/seeds/world sizes, caves/ores, lighting, day/night and weather; combat, mobs, spawning/despawning and existing persistence; sound effects/music; save/load, cached-column backing and error handling. This is a minimum inventory, not permission to omit other source features.

Preserve existing world allocation/cache limits, generation algorithms, fixed-point arithmetic, painter ordering, edit scheduling and save/storage behavior. Replace Towns hardware access with DOS adapters while retaining observable semantics: durable save/load stays durable, temporary HDD backing stays temporary, and existing supported saved state and unloaded-column edits are not lost. Existing codecs/formats should be reused where practical; any unavoidable representation adaptation must be documented and roundtrip-tested, rather than introducing the proposed new region store. No new tagging/ownership, spawning rules, generation policy or UI accompanies the port. If an unavoidable platform limitation would change behavior, raise the concrete mismatch before implementing that change.

Build/toolchain, DOS startup, input/timers/interrupts, VGA presentation, ESDI file adapters and SB1/OPL2 audio adapters are port infrastructure. Optional stage counters, reproducible routes, profiling and correctness/reference harnesses are also allowed during Phase A; measure their overhead and keep test instrumentation out of normal gameplay. They establish evidence without authorizing production optimization experiments.

Phase A ends with a runnable complete 486 package, the pinned feature/storage checklist, gameplay and save/load regression evidence, screenshots/audio checks, measured frame times/memory/I/O and an explicit user acceptance of parity. Performance findings may populate the backlog, but no FPS threshold authorizes an early renderer rewrite, x87 conversion, world redesign or other new performance change. Keep small coherent commits for the main owner to review; do not bundle Phase B into a parity fix.

## 1. What the investigation establishes

### Source findings

- `fmtowns/townscraft/src/render.c` draws a radix-sorted **far-to-near painter list**. Hidden geometry can still be collected, transformed, clipped and textured before a nearer house wall overwrites it.
- Frustum, backface, group/chunk and downward opaque-floor rejection already exist. This is not a game with no view culling. The ordinary draw path also performs bounds rejection; the distance presets add earlier per-piece checks.
- `draw_quad_split()` subdivides nearby merged quads to mitigate affine texture distortion. Large walls/ceilings close to the camera can therefore cost several polygons despite a compact greedy mesh. Added geometry, subdivision, clipping and hidden overdraw are separate hypotheses.
- General occlusion is optional/default-off. Its prepass accepts at most 16 nearby opaque patches; a coarse tile is covered only if **one patch** covers the entire tile. Several partial patches do not combine into a proof. It also runs through the nearby subdivision path, which can reduce usable patch coverage. Whole projected bounds plus conservative depth and painter-order tests must pass before rejection. This favors correctness but can miss many useful interior occlusions.
- Its proof cache is exact-pose-only. Walking/turning rebuilds the proof; standing still can reuse it. Benchmark both.
- `raster.c`/`trap.S` transparency means “skip texel zero,” not general alpha blending. Glass/water/foliage holes must remain visible. Models/entities have additional ordering constraints.
- Block edits also trigger lighting and dirty-layer mesh work, followed by face-list invalidation. Their spikes must be separated from the steady cost of looking at an already completed house.

Earlier reports rejected some prototypes using open spawn or travel fixtures. `STREAMING.md` records an opaque prepass at 16.71 versus 17.0 FPS, and an older 48-patch version slowing travel about 2%. Those results do not demonstrate that every visibility-first design loses in a dense interior. They do justify keeping an inexpensive outdoor path and measuring all overhead.

### New controlled house experiment

`tools/audit_towns_house.py` creates isolated instrumented source snapshots and runs the existing Tsugaru guest-clock benchmark. The source revision is `32133947eadc33d437f962162deda5df606d37cf`; exact source and ISO hashes are recorded in the results. The house is a 17-by-13 shell with doorway, glass window and roof, added to the same deterministic cave/tree/background terrain. The “no house” run uses the same camera locations. Geometry and lighting are cooked before sampling; there are no building edits, AI or HDD accesses.

First-run results, MODEL2 / 16 MHz / 8 MB, logical 160x100 3D viewport, 12-block view, fully textured distance, eight sampled seconds per phase:

| Camera phase | No house, culling off | House, culling off | House, existing culling on |
| --- | ---: | ---: | ---: |
| Outside static | 23.15 FPS | 10.95 FPS | 10.54 FPS |
| Inside static | 14.56 FPS | 10.93 FPS | 10.70 FPS |
| Inside moving/turning | 13.17 FPS | 10.07 FPS | 8.85 FPS |
| Inside looking up | 43.51 FPS | 14.95 FPS | 14.68 FPS |

For the interior static case, mean renderer time rises from 68.24 to 90.94 ms while update work is only 0.19 versus 0.33 ms. For looking up, renderer time rises from 22.53 to 66.44 ms with update work about 0.19 ms in both. Rasterized faces rise from 85 to 134 in the static interior and 24 to 79 looking up. Textured-span sample counts rise approximately 31% and 6.1 times, respectively; these counters include transparent span samples and exclude flat/tiny/sky work, so they are **not** a complete opaque-overdraw ratio.

This reproduces a completed-house rendering penalty independently of block-edit stalls or disk speed. The present conservative prepass is not the solution in this fixture: moving-house renderer time rises further to 112.51 ms. It does not prove that unnecessary overdraw alone accounts for all the cost, or that a front-to-back rewrite will win. Stage-level profiling may establish the Phase A baseline; production renderer changes remain Phase B.

Limitations: one run per case, a synthetic house rather than the user's house/seed, a 12-block test setting rather than the default 8, frozen simulation, and no PC/VGA port. Outside sampling still includes some mesh preparation, so it is not a pure steady-state render comparison. Inside phases are substantially settled. Motion is prescribed from emulated time, so variants sample slightly different points along the same route. No statistical significance or exact Everex FPS is claimed. The historical 2 MB run is almost identical (house moving: 10.07 FPS off / 8.85 on), so more RAM by itself does not fix this fixture. All eight cases and hashes are tracked in [the baseline data](tests/baselines/2026-10-09-house.json). The unchanged native renderer also passed all 960 floor and 5,184 general-occlusion pixel comparisons during this investigation.

The quoted “~4–7 ms” coverage saving has no verified measurement provenance here. At 10 FPS a frame costs about 100 ms; saving 7 ms would yield about 10.75 FPS, not by itself fix single-digit performance. A successful architectural change must remove materially more worst-case work or combine with other measured improvements.

## 2. Hardware and fidelity contract

Reuse the source-audited target in `../86Box/docs/86craft/1989-hardware.md`, with these qualifications:

| Component | Initial target | Validation required |
| --- | --- | --- |
| CPU/FPU | Primary: Intel 486DX/25 + integrated x87. Deferred: Everex-class 386DX/33. | Main build may use 486 instructions/x87, but not later Pentium/SSE instructions. The old 386 diagnostic uses an Intel 387 proxy; Cyrix FasMath 83D87 timing is unsupported and deferred. |
| RAM | 16 MB | This is the proposed PC baseline; do not silently use the board's 64 MB emulator ceiling. Test memory pressure separately. |
| Board | Primary: ASUS ISA-486 (`isa486`), proxy for a 486DX/25 PC. Deferred: ASUS ISA-386C for Everex-class hardware. | Neither is an exact Compaq/Everex/cache reconstruction. Both selected motherboard BIOS images identify 05/05/91; strict 1989 hardware/firmware authenticity remains unresolved. |
| VGA | Tseng ET4000AX ISA (`et4000ax`), 1 MB; user-selected Q4 1989 hardware target | No W32/W32i/W32p, VLB or PCI substitute. The pinned standard V8.06X BIOS is dated 04/15/92; treat it as a firmware proxy, not proven 1989 firmware. Exact first-batch retail board/RAM configuration remains unaudited. Measure actual framebuffer upload costs rather than assuming an FPS gain. |
| Disk | Final desired hardware: WD1007V-SE1 ESDI + CDC Wren V 94186-383 | Audit actual formatted CHS/BIOS/FAT partition capacity and emulator support. The model's 383 MB name is unformatted, not an image-size specification. Temporary diagnostic timing/media are described below and do not establish final Wren fidelity. |
| Audio | Original Sound Blaster 1.0 / OPL2, 0x220, IRQ7, DMA1 | The earlier doc chose DSP1.03; current source labels that a prototype and defaults SB1 to 1.05. Audit retail/date evidence before freezing firmware; implement the conservative DSP1.x feature set. |
| Input | AT keyboard, COM1 serial mouse via DOS mouse driver | No assumption of PS/2 mouse or Towns game-port protocol. |
| Media | HDD installation; boot/install floppy; optional period CD later | CD is distribution/content, not live terrain. No assumed ATAPI or El Torito boot support. |

At the start of this investigation, the checked-out emulator was `0579ccb9a`, based on upstream `f60098841542e2e4abca4197e7343dc356523716`; it contained the earlier hardware document, not a built/boot-tested PC VM. `DOScraft` was empty, with no usable DOS guest image, toolchain or 86Box executable under `86craft`. The scaffold now builds standalone DOS diagnostics with pinned DJGPP and private FreeDOS media for official 86Box v6.0 b9001. On 2026-10-09 the platform guest booted and passed CPU-floor, x87 initialization, 8 MiB heap, zero swap, timer and 32 KiB HDD roundtrip checks. The Mode X probe passed four-plane readback and automatic text restoration; its full-border pattern was visually inspected. These are infrastructure checkpoints, not a playable game or a completed keyboard/mouse/audio parity gate. [Recorded evidence](tests/baselines/2026-10-09-pc-platform.json).

Runtime configuration finding, 2026-10-09: **official 86Box v6.0 b9001 has no `CDC94186383` timing preset**; that preset exists only in the newer fork. Supplying it to the pinned official runtime silently rewrites the disk timing to RAM Disk, invalidating period-disk measurements. Diagnostic profiles now use supported `1989_3500rpm`, explicitly a **generic 1989 timing proxy**, using a temporary **20 MiB scratch disk**. Neither the proxy nor the scratch capacity is the final Wren specification. Retain the desired WD1007V-SE1 ESDI + CDC Wren V hardware target, and audit a supported final timing configuration before claiming fidelity. The runtime-written configuration retained the generic timing after the verified DOS runs. First boot requires matching floppy/HDD geometry and **`A:, C:` boot order**: the empty HDD boot code with an MBR signature explains the earlier summary-screen hang when set to HDD-first. Working private images were preserved. Computer use resumed with user permission; automatic display completion and disk reports are verified, but injected Escape has not established a guest keyboard-input gate.

VGA selection, 2026-10-09: the user accepted the original ET4000 ISA's Q4 1989 batches as eligible and replaced the provisional Video 7 target. The [ET4000 manufacturer databook](https://ardent-tool.com/datasheets/Tseng_ET4000_Databook_1990.pdf) describes the original controller; the [stable emulator device implementation](https://github.com/86Box/86Box/blob/4fef696a4eead1d55a28d6ac0e5bd2864e5454da/src/video/vid_et4000.c) supports ISA ET4000AX, 256/512/1024 KB and selectable standard BIOS revisions. Use indexed 256-color VGA initially; do not rely on later high-color RAMDAC features exposed by the generic emulator model. The 1 MB choice is a development target supported by the controller/emulator, not a verified specification of a particular 1989 retail board. New test VMs use the updated tracked profile; old private scratch VMs are preserved, not silently rewritten.

Freeze an emulator revision, ROM hashes, exact config, disk geometry/timing, DOS/extender/driver versions and binary hash per release. Use the accurate 386 interpreter; the optional 486 interpreter is faster on the **host** but explicitly less accurate. Use period HDD timing, not RAM Disk. Verify the host sustains full emulation speed, otherwise separate guest-clock results from wall-clock responsiveness. x87 softfloat and PIT choices must be logged and consistent. [86Box machine settings](https://86box.readthedocs.io/en/latest/settings/machine.html), [disk settings](https://86box.readthedocs.io/en/latest/settings/hdd.html).

The historical hardware evidence already lives in the hardware document; this plan does not upgrade its provisional combinations into a claim of perfect 1989 authenticity. Modern development tools/algorithms are assumed acceptable for this hardware demake. Strict 1989 guest-software authenticity would require a separate runtime decision.

### Current target decision: 486 primary; 386 port deferred

Latest user decision, 2026-10-09, supersedes the earlier 386-primary and immediate dual-support decisions: **486DX/25 is the primary demake target**. Complete and accept its direct port first, then optimize separately. The Everex-class **386DX/33 remains a long-term compatibility/port target**, but work on it begins only after the main demake's correctness/performance is satisfactory. It is not a Phase A or Phase B release gate; a separate later build or backend is allowed if necessary.

The default scaffold builds with `-march=i486 -mtune=i486 -m80387 -mfpmath=387`, enabling 486-specific instructions and x87 code generation. Do not expect that executable to run on a 386. Keep existing integer/fixed-point algorithms initially; compiler permission is not a request to replace rendering arithmetic wholesale. Use no blanket fast-math option. The 486DX has integrated x87 and does not accept a separate Cyrix FasMath as an upgrade.

The primary profile selects `isa486`, `i486dx`, 25 MHz and `fpu_type=internal`, retaining 16 MB RAM and the agreed ET4000 ISA/Sound Blaster/ESDI peripherals. The chosen ASUS ISA-486 is a supported ISA-only Socket 1 proxy, not an exact Compaq Deskpro or a newly proven 1989 motherboard. Its ROM is SHA-256-pinned and DOS diagnostic boot is now verified. The optional `--target 386dx33` preserves the old diagnostic under a separate output directory; it is not evidence of a playable compatibility build.

For future hardware-only comparisons, use a 386-compatible executable on both CPUs and keep its hash, DOS/extender, RAM, VGA, disk timings, seed, graphics settings and route identical where supported. Compare a 486-tuned build separately: changing CPU and compiler together cannot isolate either gain. Record unavoidable motherboard differences and emulator/cache limitations. Select a genuine emulated CPU, not the optional 486 interpreter on a 386 profile. Keep dynamic recompilation disabled and log FPU/PIT settings and emulation speed.

Cyrix request: the intended historical 386 coprocessor upgrade is the **FasMath 83D87**, not the SX variant or later 40 MHz revision. The [1989 manufacturer manual](https://www.arithmazium.org/classroom/lib/Cyrix_FasMath_83D87.pdf) documents 80387 pin/software compatibility and 20/25/33 MHz operation. The pinned 86Box CPU/FPU tables expose only `none`/`387` for Intel 386DX and select Intel 387 timing for `387`; there is no separate FasMath model. The existing diagnostic remains explicitly Intel 387, with no invented 50% timing adjustment. Calibrated Cyrix emulation and its compatibility testing are deferred, not blockers for the 486 port. An advertised FPU improvement is not an overall game-FPS multiplier.

Historical qualification remains: do not label the primary proxy an authentic, purchasable-in-1989 Compaq reconstruction. The Deskpro 486/25 was announced in November 1989, but a [contemporary availability report](https://www.computerwoche.de/article/2780242/mini-plaene-compaq-wird-pc-markt-zu-eng.html) gives January 1990. Exact board/firmware and retail availability still need an audit. The user's architecture choice is not proof of historical shipping dates.

## 3. Runtime and platform split

### Default: DOS + DJGPP/CWSDPMI

Prefer a 32-bit protected-mode DOS executable using a pinned host-side DJGPP cross-toolchain and CWSDPMI. It fits the existing GNU C and GNU-style 386 assembly and provides ordinary HDD files, extended memory and access to PC devices. Build on the host; do not compile the game on the emulated target. The bundled Zig freestanding compiler is useful for current tests, but it is **not** already a DJGPP DOS toolchain. [DJGPP project](https://www.delorie.com/djgpp/), [cross-compiler documentation](https://www.delorie.com/djgpp/v2faq/faq22_9.html).

Open Watcom plus a compatible 32-bit extender is a fallback if the cross-toolchain/setup is impractical or measured runtime overhead is excessive. It entails compiler/inline-assembly/ABI adaptation. A bare-metal PC kernel remains an escalation only after measuring a DOS bottleneck: otherwise it adds boot, FAT, memory, BIOS bridging, input, interrupt and storage driver maintenance before improving the renderer. [Open Watcom programmer's guide](https://open-watcom.github.io/open-watcom-v2-wikidocs/cpguide.html).

Use a minimal DOS boot with no unnecessary TSRs and no paging. Detect usable RAM, allocate fixed arenas at startup, lock interrupt code/data/stacks and fail clearly if the budget is unavailable. No allocator, DOS file operation or expensive game update in an ISR. PIT/keyboard/audio IRQ handlers only acknowledge and enqueue bounded work; preserve and restore vectors/hardware on normal exit and error paths. Profile protected/real-mode transitions. [DJGPP interrupt pitfalls](https://www.delorie.com/djgpp/v2faq/faq18_11.html).

### Code boundaries

| Existing area | Port treatment |
| --- | --- |
| Blocks/items/textures/font, inventory/crafting, collision/player rules | Reuse with type/header separation and regression tests; preserve behavior. |
| `common.h` includes Towns `hw.h` | Split portable fixed-width types/math from platform I/O; core must not include Towns ports. |
| `crt0.S`, `isr.S`, linker/boot ISO code, heap/sys layer | Replace with DOS startup/extender and PC platform implementation. |
| Keyboard/mouse/time | Map AT input to semantic actions while keeping the same bindings; full key-up state, extended keys, lost-event handling and mouse deltas. |
| `video.c`, hardware page flipping in `gfx.c` | Replace display/presentation; keep drawing/font/HUD behavior separately. |
| `render.c`, `raster.c`, `trap.S` | Phase A: port the existing painter/fixed-point baseline, including inherited optimizations. Adapt object format/symbol decoration/ABI and test assembly against C. Independent production candidates wait for Phase B. |
| World generation/cache, Towns HDD/floppy drivers and `save.c` | Phase A: retain existing generation, cache ownership, codecs, save state and storage semantics; adapt hardware I/O to DOS. New region formats, local generation and enlarged worlds wait for Phase B. |
| YM2612/RF5c68 sound | Replace with OPL2 and Sound Blaster mono PCM; preserve composition/effect identity as far as hardware permits. |
| Host/native test harness | Keep a software reference/headless mode. A host window or GPU must never do guest rendering for scored results. |

Copy a pinned Towns source baseline into the port or use explicit vendor imports with an import manifest. Avoid a live build dependency on whatever happens to be dirty in the Towns directory. Future fixes are cherry-picked/reviewed rather than silently inherited.

Suggested eventual layout: `src/core/`, `src/world/`, `src/render/{reference,painter}/`, `src/platform/dos/`, `tests/`, `tools/`, `profiles/`, `releases/`, and ignored `build/`/`runtime/`. A `coverage` candidate belongs to Phase B; directory organization does not require a source redesign during Phase A. The game belongs here, not inside the separate 86Box repository. Keep ROMs and mutable OS/world images out of Git; track configs, scripts, provenance and benchmark summaries.

## 4. Phase A display port; Phase B rendering experiments

### VGA backend first

1. Initial platform proof uses BIOS Mode13h (320x200, 256 colors), RAM rendering and measured framebuffer upload. This is a test pattern/renderer probe, not permission to shrink or redesign the game's HUD.
2. Phase A preserves the Towns 320x240 game layout (200-row view plus 40-row HUD) using a tested unchained VGA mode. Implement the required presentation adapter and validate pitch, palette, aspect/FOV and existing scale options. Mode13h is only a probe, not the shipping game-layout decision.
3. Keep pitch/viewport/scale explicit rather than baking in Towns' 512-byte VRAM stride. Select a straightforward compatible presentation path for the port and measure render, conversion/upload and retrace wait separately. Competing RAM/direct-VRAM, planar-copy and page-flip performance designs belong to Phase B.
4. Phase B may investigate half-resolution upload exploiting doubled pixels, plane masks and VGA vertical replication. Preserve HUD clarity and exact aspect/FOV; this is a candidate, not free bandwidth or an assumed default.

Mode X exposes VGA page-flip/plane opportunities but is not a drop-in linear framebuffer, as Abrash's own discussion emphasizes. Its publication date does not make it later hardware. [Abrash, Mode X](https://github.com/jagregory/abrash-black-book/blob/master/src/chapter-47.md).

### Phase B competing renderers (after full parity acceptance)

R0 is the Phase A production baseline. R1-R4 are deferred experiments, not port milestones. Prioritize the first three attachment proposals together as an architectural group: prove near-to-far traversal, compare row spans against a bitmask, then measure hierarchical tile rejection. Keep depth-reference testing separate from production adoption and measure each added stage independently.

**R0 — portable painter baseline.** Same visible content, view distance, resolution, lighting and poses as Towns; correct display conversion and counters. This establishes the cost of the PC platform before an algorithm change. Preserve greedy merging, cached integer camera tables, early bounds tests and effective floor rejection.

**R1 — bounded hierarchical rejection.** Frustum-test subchunk bounds once; add conservative opaque occupancy/air-connectivity metadata updated only for edited chunks. Reject whole groups hidden by proven near surfaces before face projection. Doorways, cave openings, glass and unloaded neighbors must stay visible. Do not run a global visibility rebuild on each frame or block edit. Try cumulative coarse coverage as well as the current single-patch restriction, retaining depth bounds; partial tiles are never simply declared opaque.

**R2 — depth reference/contender.** Fixed-point reciprocal-depth buffer with span-level early rejection and texture sampling only after passing depth. At logical 160x100, a 16-bit buffer is 32,000 bytes and a coverage bitset 2,000 bytes; at 320x200 they are 128,000 and 8,000 bytes. These are just 3D buffers, excluding color/HUD/metadata and allocation rounding. RAM is available, but per-pixel arithmetic/read/compare/store on the 486DX/25 may cost more than the saved texturing. Derive quantization/range and stable shared-edge/tie rules; use higher precision in the correctness reference where needed.

**R3 — visibility-first opaque spans.** Traverse demonstrably front-to-back opaque terrain, subtract already covered row intervals or tile masks, shade only uncovered portions, and retain depth for objects that cannot use that ordering. Compare bounded row-span lists against bitsets/coarse full-tile masks. Span fragmentation has a fixed capacity and a correctness-preserving fallback, not memory growth or silently missing geometry. Generate depth while emitting visible spans if cheaper than a second pass. Do not just reverse a center-distance sort: overlapping/near-clipped merged faces and moving rotated models require an ordering proof or depth resolution.

**R4 — perspective/subdivision alternative.** If near split/setup cost dominates, compare axis-plane/projective incremental mapping with reciprocal correction at short span intervals against existing affine per-cell subdivision. Preserve perspective/FOV and texture identity. Test cracks, extreme pitch, near planes and thin features. Fewer polygons may justify extra span arithmetic, but must beat the primary 486 baseline.

Abrash reports that Quake emitted nearest spans to reduce overdraw and stabilize frame rate; this motivates the experiment, not a Quake-sized engine transplant or an FPS prediction. Static BSP/PVS assumptions do not transfer directly to a world the player edits continuously. [Abrash's CGDC rendering talk](https://www.gamers.org/dEngine/quake/papers/mikeab-cgdc.html).

A full 3D voxel DDA raycaster is a fallback research branch if the span pipeline fails: it avoids mesh processing but scales with pixels times traversed cells and complicates models/cutouts. A heightfield raycaster or Doom-style 2.5D world would lose caves, houses and vertical looking and is not an acceptable shortcut. A global precomputed BSP/PVS is likewise not the default for arbitrary building. No renderer is chosen on novelty alone.

### Transparency/ordering contract

- **Opaque:** may establish coverage only for pixels the raster rules actually fill. Bounding rectangles are not coverage. Holes, shared-edge cracks and near-plane gaps must not hide geometry.
- **Cutout:** zero samples do not write color, depth or coverage. Nonzero samples may write depth. Opaque terrain followed by depth-tested cutout/model passes is a straightforward correct starting point; it may leave some removable foliage overdraw, which is acceptable until measured.
- **Moving/intersecting entities and custom models:** depth-test against terrain and each other, or use a proven visibility ordering. A binary “terrain already covered here” mask alone cannot distinguish a model in front of a wall from one behind it.
- **True blending, if later introduced:** render ordered blended layers after opaque depth, testing depth but not treating partial alpha as full occlusion. This is not currently required and must not be added as an unrelated visual change.
- Glass/water cutouts behind glass, foliage, torches, rotated mobs, doors, camera-inside-water and block removal opening a sightline are required adversarial tests. Prove no stale coverage after edit, seam publication or camera movement. When mapping/rounding intentionally changes, use an independent high-precision reference plus reviewed golden images; pixel parity with the old painter alone is not a universal correctness oracle.

## 5. Phase B world, mesh and HDD redesign (deferred)

Everything proposed in this section is research after Phase A acceptance, not a prerequisite for the direct port. Phase A retains existing world limits, cache/generation behavior, save semantics and mob rules through DOS adapters. Region persistence, larger worlds, generation changes and new entity ownership require their own correctness review and any needed user choice; optimization is not authority to add gameplay/UI rules.

### Bounded working set, independent of total world size

Use 16x16 horizontal columns with the existing height48 initially; chunks can remain 16 cubed for mesh/light locality. Separate authoritative block data, derived light, mesh, renderer visibility and save ownership. Maintain a coordinate-to-resident-slot index and fixed queues/pools; no per-frame global world scan.

One column's one-byte blocks plus one-byte light is 24 KiB. A 9x9 resident neighborhood costs 1.90 MiB, or 13x13 about 3.96 MiB, before meshes/heights/metadata. Prototype sizes by measured prefetch time and motion, not simply filling all RAM. A 16 MB target budget can tentatively reserve 4 MiB resident terrain, 2 MiB meshes, 1 MiB generation/light scratch, 1 MiB compressed cache/I/O and under1 MiB rendering/assets, leaving several MiB for DOS/extender, heaps and headroom. Measure actual free physical memory; this is a budget proposal, not an implemented allocator.

Keep global coordinates32-bit and draw/transform coordinates camera-relative. Current packed draw items use eight-bit absolute X/Z; store directories and mob visitation are fixed256 entries, and world allocation selects at most width256. These cannot be enlarged just by changing a constant. Audit ordering keys, squared-distance arithmetic, terrain noise tables, save lengths and overflow at coordinates255/256,1023/1024 and2047/2048. With FU4096,2048-block coordinates fit32-bit, but some products of distances do not; bounded local distances or explicit64-bit intermediates are needed.

Uncompressed terrain at height48 is 48 MiB for1024x1024 and192 MiB for2048x2048, **blocks alone**. Retaining all light doubles that. At2048, block+light would exceed a roughly383MB-unformatted drive before DOS, indexes, extra worlds or edits. Compression/on-demand creation is required; no promised compression ratio. World capacity is unrelated to how much geometry should be rendered locally.

### Durable region store

Use DOS HDD files grouped into moderate regions, an in-memory compact index and append-only changed-column/entity records with lengths, version/generator ID, sequence and checksums. Commit an index/journal record only after its data is written; recover the last valid record after interruption. Cache decoded neighboring columns and compressed ahead-of-player records; batch contiguous region reads and measure seek costs. Restrict formats to the chosen DOS filesystem and signed seek/API limits, split region files well below2GB, and handle disk-full/short I/O explicitly.

Derived meshes are disposable. Lighting can be recomputed or selectively cached; initial generation/import can bake it. A proposed Phase B durable store must record all state required by the accepted gameplay contract, not just a temporary disk scratch directory. Existing Towns HDD backing is explicitly temporary; Phase A preserves that distinction alongside the existing durable save path. Replacing both with a region store is a separate redesign. Do not overwrite unrelated images or claim to save before reload/recovery tests pass.

DOS file calls are synchronous. A queued read is not automatically asynchronous, and a “2 ms budget” cannot interrupt a blocking seek. Start with multi-ring prefetch, large RAM slack and startup warming; log cold worst-case seeks. If that still produces frequent traversal hitches, evaluate a controller-specific asynchronous backend behind the same storage API. That is an explicit measured escalation, not a promise that background scheduling fixes blocking DOS I/O.

### Generation choices

Preferred path: random seed, deterministic **region-local** generation, ahead-of-player cooking/cache onto HDD and a warm startup neighborhood. Keep generator version immutable in an existing save. Coordinate-hashed features plus bounded feature halos produce the same trees/caves/ores regardless of visit order. The current world-wide RNG-driven cave worms and feature loops cannot be called independently per region and expected to match; either retain them in an offline cooker or redesign them with owner regions and bounded influence. Determinism tests cover reverse/random generation order and region borders.

Offer pre-cooked seed packs on CD/install media through the same store interface. Compare decompression+HDD read against CPU generation at cold and warm cache states. Full2048-world cooking before play is an optional measured mode, not a prerequisite: area is64 times a256 world, so a linear extrapolation can suggest cost but cannot establish a30–60second cooking time. The user's willingness to wait30–60seconds supports a substantial startup warm-up, not an assumed guarantee of cooking everything in that interval. No whole-world block scratch allocation.

### Mesh/edit scheduling

Keep greedy meshing and dirty-layer rebuilds initially. Separate urgent player-visible geometry from lower-priority lighting/new-column preparation. Publish coherent meshes atomically; an edited block must disappear or appear in the next rendered frame. Do not reintroduce the old multi-frame removal delay in pursuit of average FPS. First measure immediate local rebuilds. If expensive, prototype a bounded edit patch/overlay or smaller rebuild units, with seam and stale-face tests; do not simply leave a stale mesh visible.

Prefer fixed size-class/slab or per-chunk pages over a global compacting mesh pool if fragmentation spikes are measurable. Keep mesh/visibility versions per chunk plus relevant neighborhood signatures, so one distant publication does not unnecessarily invalidate every stationary renderer cache. Bound generation/light/mesh jobs by resumable units and CPU time, but separately account for uninterruptible disk calls. On cache reuse, cancel stale jobs and preserve unsaved edits before eviction.

### Mobs

Phase A preserves current spawning/despawning, dormant state, live fuses/explosions and saved mob state. The proposed Phase B policy for distant hostiles, player-kept friendly animals and seeded regional wildlife must be compared with that baseline and reviewed separately. A future ownership/tag signal is not an existing feature to invent during the port; do not add fence detection, tagging/taming UI or new rules without approval.

Audit current passive scarcity independently: source does a one-time seeded population attempt, rejects half of regions immediately, requires grass, and marks fully inspected empty regions visited. Readiness, slot limits, seed/biome and dormant ownership can affect encounters. This suggests test cases, not a proven diagnosis of the user's seed. Preserve initial behavior for port parity, instrument attempt/readiness/grass/cap reasons, then propose any spawning rule change separately.

## 6. Phase A audio port; Phase B coprocessor experiments

Sound Blaster1.x does not provide Towns' eight independent PCM playback channels. Start with original effects resampled to unsigned8-bit mono, a bounded software mixer and single-cycle DSP1.x DMA blocks; no assumed DSP2 auto-init, SB Pro stereo or SB16 commands. Choose a low initial sample rate and test gaps/latency while rendering slow frames and reading disk. Allocate physically suitable conventional-memory buffers that do not cross64KiB DMA boundaries; lock all IRQ data. The existing86Box source gates auto-init command0x1C on DSP2.0+. [Pinned DSP implementation](https://github.com/86Box/86Box/blob/f60098841542e2e4abca4197e7343dc356523716/src/sound/snd_sb_dsp.c), [DJGPP DMA guidance](https://www.delorie.com/djgpp/v2faq/faq18_13.html).

Adapt music to OPL2 instruments/sequencing; keep synthesized effects and sample generation off the frame hot path. Profile music only, effects only, combined and silent runs. Reissuing short single-cycle buffers and doing software mixing are real extra PC costs; shipping sound is not allowed to silently destroy the frame budget.

Complete all existing effects/music in Phase A; audio is not postponed until renderer optimization. Keep integer/fixed-point transforms, lookup trig and reciprocal tables throughout Phase A. Existing Towns builds disable x87 and have cheap additive camera transforms, so an available FPU does not automatically help. Compiler permission for 486 instructions/integrated x87 is not arithmetic-conversion authorization. Only in Phase B test batched transforms, projection or span perspective correction with actual conversion/memory costs; compare fixed-point versus x87 correctness/tolerances and guest timings. No per-pixel floating-point rewrite without evidence, no Pentium/SSE instructions in the main 486 build. A future 386 port must exclude unconditional 486-only instructions and be measured independently; it need not share the main executable.

## 7. Standard benchmark and acceptance gates

Create Phase A profiling/correctness infrastructure with a versioned generated test world and route format, checksummed geometry, seed, spawn, camera paths, edits and random state. Compare the port with the pinned source baseline at matching settings. Phase B candidates later consume the same content and input. Frame-count-independent routes support fair throughput comparison; add exact fixed-pose/command replay for pixel/work-count comparisons. Run each segment independently and together:

1. Open terrain walking; level looking; walking looking fully down and fully up.
2. Outside/inside a completed opaque house, roof/floor views, doorway/windows; then two-story and several dense adjacent houses.
3. A deliberately hidden dense world behind a close opaque wall, with no AI/edits. This exposes saved hidden work rather than just extra visible wall texturing.
4. Identical house with glass/water/foliage, open/closed doors, moving rotated mobs crossing the sightline and near-plane intersections.
5. Breaking/placing individual blocks, a rapid building sequence, torch removal/placement, chunk seams, and removal of an occluder while moving.
6. Cold/warm HDD traversal across boundaries, sudden turns/revisits, dirty eviction and disk-full/error/recovery.
7. Seeded live passive/hostile populations and existing dormant/saved-mob unload/reload behavior, with audio-on/off variants. New protection/ownership policies are Phase B cases only if separately approved.

Record guest frame-time median/p95/p99/max, percentage over66.7/100ms, time from accepted edit to visible geometry, presented frame age, CPU stage times (game/light/generation/mesh/collect/cull/transform/clip/span/depth/texture/upload/wait), high-water RAM, queue debt, mesh/list overflow, projected faces, shaded/hidden samples, disk requests/bytes/latency and audio underruns. Count texture samples separately from actual nonzero writes and unique covered pixels. No modern host wall-clock loop timer substituted for target guest performance, no RDTSC on the 486 or deferred 386, no FPS-only summary.

Separate initialization/cooking from traversal and steady-state house rendering; explicitly wait for needed meshes/jobs to finish in static tests. Make profiling optional and measure its overhead. Keep the existing correctness tests (floor960 views, distance1920 and general5184) as inherited coverage; add house, diagonal/edge, edit invalidation and randomized geometry tests against a depth reference.

Phase A acceptance is complete feature/storage parity and a runnable measured baseline, explicitly accepted by the user. Provisional Phase B performance goal: sustained at least15FPS in the agreed house/walking suite with no routine single-digit sections, and no repeated streaming stalls over100ms. This is a research target, **not a promised achievable result or a reason to optimize before parity acceptance**; freeze view/resolution/workload before judging it. Preserve the source's next-presented-frame geometry for ordinary edits and measure its wall-clock latency in both phases.

Use at least three repeats, report uncertainty and per-scene regressions. The current primary baseline (486DX/25,16MB, agreed video/disk) gets priority; the 386 port is deferred and historical Towns2MB/16MHz is diagnostic, not an imposed PC requirement. Do not accept a high-setting gain purchased with a clear primary-baseline regression. A candidate neutral within measured noise on baseline and clearly better at high settings can stay, as requested. Disable or adapt extra visibility work outdoors only by reproducible scene/work criteria, not fragile “inside house” heuristics. Once the 386 port starts, define its own acceptance settings without silently lowering the main game's settings/content now.

## 8. Milestones, deliverables and stop points

| Milestone | Deliverable | Completion gate |
| --- | --- | --- |
| A0: baseline inventory/plan | Pinned source, complete feature/storage checklist, this plan and separate backlog | Inherited behavior/methods distinguished from proposals; Towns untouched. |
| A1: reproducible PC platform | Pinned cross-toolchain, clean build, legal guest boot, config and launch script; test pattern/input/PIT/RAM/file roundtrip | Boot on exact 486 profile; no paging; repeatable timing; exit restores DOS state. |
| A2: parity vertical slice | Small playable world and existing painter renderer; original controls, 320x240 HUD/layout, full pitch and immediate edits | Screenshot/rules regressions; display upload profiled; ABI/assembly parity; not full parity acceptance. |
| A3: complete direct port | Every existing gameplay/menu/graphics/audio feature and existing generation/cache/save/storage semantics via DOS adapters | Complete checklist, gameplay replay, save/load/restart and error tests, audio/input checks; no new renderer/world/gameplay policy. |
| A4: parity acceptance/release | Runnable versioned 486 package, feature/storage evidence and baseline measurements | User explicitly accepts full parity; retain known-good release and launch-from-any-directory checks. This gates all Phase B work. |
| B1: renderer architecture investigation | Correct near-to-far traversal, span/bitmask comparison, then hierarchical tiles; independent stage reports | Starts after A4; correct cutouts/models/holes; measured benefit and per-scene regressions; retain baseline if candidates lose. |
| B2: world/storage redesign | Separately scoped larger-world/region/local-generation prototypes | Starts after A4; save-state/seed compatibility policy reviewed, bounded RAM, seams/edits/recovery and cold-disk evidence. No implicit draw-distance change. |
| B3: measured tuning/release | Remaining raster/VGA/cache/simulation/x87 experiments from backlog; accepted package/report | Starts after A4; one demonstrated hotspot/change at a time, correctness and baseline regression gates. No guaranteed gains. |
| Deferred C: Everex-class compatibility port | 386-compatible build/backend and audited coprocessor profile | Begins only after the main demake is satisfactory; actual boot/gameplay/pixel/performance checks. |

A0-A4 complete the direct port before B1-B3 start. Phase A profiling/reference harnesses may gather evidence and refine the backlog, but production renderer prototypes, large-world redesign and new performance changes wait for A4. Make each port step or later experiment a small coherent commit with its relevant checks/results; milestones may require several such commits. Helpers may own bounded tasks, with explicit file ownership in the shared workspace. The main owner controls staging/committing; this documentation task does neither. Keep accepted baseline releases and separately selectable Phase B experiments.

No credible calendar estimate yet: DOS toolchain/ROM availability, framebuffer cost and complete parity effort are unmeasured. Estimate remaining Phase A work after the vertical slice; estimate Phase B separately after parity acceptance and its measured experiments.

## 9. Release/repository safeguards and remaining choices

- Parent Townscraft repo tracks the DOScraft game/plan; the86Box fork remains independent. Emulator modifications are unnecessary initially and require separate rationale, tests and commits if introduced.
- Track manifests/scripts/source/test fixtures/small result summaries, not live world/OS images or downloaded ROMs. Immutable release packages include commit ID, binary/media hash and profile. Any mutable runtime image is per-release/copy-on-run, never shared concurrently by two emulators.
- A click launcher resolves paths from its own location, validates required emulator/ROM/guest files, uses only named image files, does not format/replace unknown disks, and leaves actionable errors visible. Test paths with spaces and invocation from another directory. It runs the selected manifest's build rather than whichever root ISO happens to exist.
- Preserve the Towns renderer/game and historical launchers. The pending TownsISO publication remains unrelated and uncommitted; this task does not finish or push it. GBA/SNES remain out of scope.
- Phase A preserves the baseline's supported save state/versions and storage semantics through DOS adapters, with documented byte-format/platform limitations and roundtrip tests. Retain original saves untouched. A new region-format importer/compatibility policy is Phase B, not a reason to drop existing saved state during the port.
- Phase A defaults: DOS/DJGPP, all existing gameplay/features/storage semantics, 320x240 game layout, existing world limits/generation and painter/fixed-point methods, and the fixed 486DX/25 +16MB +ISA ET4000AX 1MB +ESDI +SB1 target. Phase B renderer/world/x87/performance work waits for explicit full-parity acceptance. Ask before required UI/resolution changes or new gameplay/storage rules; documentation proposals do not authorize those changes. Do not substitute PS/2/MCA graphics guidance from the attachment for the agreed ISA card.

## Reproduction

From the workspace root:

```powershell
python .\86craft\DOScraft\tools\audit_towns_house.py --ram 8
python .\86craft\DOScraft\tools\audit_towns_house.py --ram 2
```

Uses the existing bundled Towns Zig compiler, pyelftools and Tsugaru headless emulator. Raw hashes, frame summaries, guest memory dump and final screenshots live under ignored `build/house-*`; no production ISO is replaced. `HOUSE_PRESENT` changes only the test level and `HOUSE_OCC` toggles the already-existing culler in the generated fixture. It is a Towns renderer investigation, not an 86Box runtime or a shipped optimization.
