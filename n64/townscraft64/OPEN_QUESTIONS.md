# Townscraft64: open questions

Decisions the user needs to make. Each lists a recommendation and the
milestone it blocks. Record answers here with the date.

## Q1. Which game is the reference? — ANSWERED 2026-10-15

**Decision: the current DOScraft game.** Phase A reproduces DOScraft's
feature set, not only the Towns one.

Background at decision time: the Towns source has had no commits since
DOScraft pinned it (`d706a67d`), so DOScraft's vendored snapshot *is*
current Towns. DOScraft has since added (about 170 commits) chickens,
offline biomes, resolution-selected textures, a first-person arm and held
items, a 20-minute day, mob despawn beyond 32 blocks, prebaked streamed
1024x1024 worlds with a V3 edit log, distant terrain, Low/Medium/High menus,
multi-song MIDI music and a hand-picked sound bank. Many of these live in
`src/platform/dos/` beside VGA/IRQ/Sound Blaster code, or in offline Python
tools, so each must be extracted, not copied.

Options that were considered: pure Towns, or Towns plus selected DOScraft
features.

Consequences: Q2 (RAM) and Q6 (saves) become harder, because DOScraft's
large world and edit log assume 8–16 MB RAM and a writable hard disk. See
PORT_PLAN.md §3.

## Q2. Base 4 MB or Expansion Pak? — ANSWERED 2026-10-15

**Decision: the full game runs on a base 4 MB console; the Expansion Pak
(8 MB) unlocks new features.** Push hard to fit DOScraft parity in 4 MB.
The 64DD Expansion Disk path (Q6 amendment) is the one feature that
requires 8 MB, because the 64DD shipped with the Pak. Other 8 MB extras
(bigger caches, longer view, and so on) are decided case by case, each
detected at boot and costing nothing on 4 MB.

Earlier status, kept for the record:

- 4 MB is every console and the honest 1996 target.
- 8 MB (1998 accessory, needed by Majora's Mask) roughly doubles world and
  mesh space.

Status 2026-10-15: **deferred for the cartridge**; the user is unsure and
will discuss it later. The optional Expansion Disk path (Q6 amendment)
requires the Expansion Pak, so that path is 8 MB. With DOScraft as the reference (Q1) the world itself lives in ROM, so
RAM limits only the resident working set (nearby columns, light, meshes,
mobs, edit log): roughly 2.5–3 MB free on 4 MB vs 6.5–7 MB on 8 MB
(*estimates*). Options on the table: decide after N3 measurements
(recommended), 4 MB with the Pak as a Phase B bonus, or require the Pak
(like Donkey Kong 64 / Majora's Mask). Until decided, build for 4 MB and
measure the working set at N3.

## Q3. SDK — ANSWERED 2026-10-15

**Decision: libdragon.** Legal risk was not a factor (builds are mostly
private). The user's rule: the project is about squeezing out every last
millisecond, and since both SDKs allow custom RSP microcode and custom
code paths, libdragon's tooling wins.

Considered: libultra + F3DEX3 (proven commercial 3D path, MIDI-style
sequence player, SM64/Kaze ecosystem; older tooling).

Still open, decided by measurement at N1/N3: the 3D layer (libdragon's GL
port, tiny3d, or our own microcode). Performance-first means custom RSP
microcode is in scope whenever measurements justify it. Music needs our own
MIDI-style sequencer on the libdragon mixer.

## Q4. Visual style — ANSWERED 2026-10-15

**Decision: DOScraft look in Phase A; N64 extras later.** Phase A uses
point-sampled textures, per-face light and no RDP anti-aliasing, matching
DOScraft. Phase B may add bilinear filtering, fog, smooth light and similar
as graphics presets, each only if it earns its measured cost.

Cost notes that drove it: bilinear and perspective correction are about free
on the RDP; fog is cheap; mipmapping/trilinear needs 2-cycle mode (about
half the fill rate); RDP AA reads the framebuffer back (bandwidth); smooth
light/AO breaks greedy merging and multiplies triangles.

## Q5. Controls — ANSWERED 2026-10-15

**Decision: the official interface is the Japanese N64 Mouse (NUS-005)
plus the Randnet keyboard**, keeping DOScraft's keyboard + mouse controls.
The Turok-style controller mapping below is the fallback for
controller-only players.

Implementation notes (verify at N1/N3):

- Both are Joybus devices in controller ports, so keyboard + mouse take two
  ports.
- libdragon's joypad API has a mouse style (`JOYPAD_STYLE_MOUSE`).
- For the keyboard, libdragon only defines the raw Joybus read command
  (`0x13`); we write the driver. The N64brew wiki marks that command's
  layout as needing verification.
- The keyboard has a Japanese (JIS) layout; map DOScraft's bindings by key
  position.
- Emulator support for the Randnet keyboard is unconfirmed. If ares lacks
  it, plan either an emulator patch (as DOScraft patched 86Box) or
  real-hardware testing. Mouse support in ares also needs checking.

Fallback controller mapping (Turok-style):

| Input | Action |
| --- | --- |
| Analog stick | Look |
| C-buttons | Move / strafe |
| Z | Break / attack |
| R | Use / place |
| A | Jump / swim |
| B | Sprint (hold) |
| L | Inventory |
| D-pad left/right | Hotbar slot |
| D-pad up/down | Crafting / menu |
| Start | Pause / help / settings (replaces PF keys) |

## Q6. Save model — ANSWERED 2026-10-15

**Decision: cartridge only.** Base world in ROM; edits held in RAM as a
compact block-level store; that same store is written to 128 KB FlashRAM on
save, with player, inventory and mobs. The edit cap is one budget, enforced
in one place, and shown to the player.

Why not DOScraft's format: DOScraft (V3 edit log) writes whole 16x16x48
columns (RLE, ~3.3 KB average, sector-aligned) because its columns page out
to a writable HDD. That is a paging design, not a save-size choice. With no
writable bulk storage, an evicted edited column has nowhere to go but RAM,
so the block-level format is needed for the live world as well as the save.
Proposed record: changed blocks per column (~3 bytes each), or the RLE
column when that is smaller. Capacity *estimate*: ~40,000 changed blocks.
Light is recomputed on load, as DOScraft already does for edited columns.

Rule for compatibility paths (user, 2026-10-15): support an alternative
backend only if it is clearly better than the default, and only if it costs
nothing on the frame path. Any backend switch sits at the column
page-in/page-out and save boundary, never per block access; prefer a
separate ROM build (or an overlay) to keep RAM free.

Measure at N5: real edit volume from play sessions against the budget.

### Q6 amendment, 2026-10-15: optional Expansion Disk

**Decision: support a 64DD "Townscraft64 Expansion Disk" as an optional
path**, modeled on the F-Zero X Expansion Kit (cartridge game + 64DD disk,
Japan 2000). The cartridge stays the complete game; the disk adds:

- several saved worlds (the minimum the user wants from it);
- effectively unlimited edits: edited columns page out to the disk, as
  DOScraft pages them to its HDD, instead of living in the RAM edit budget;
- room for later extras ("among other things"), each decided separately.

The user's goal: Townscraft64 should be the N64 game that outdoes every
other "N64 killer".

Constraints, from the compatibility-path rule above:

- Zero cost to the cartridge path: backend switch only at column
  page-in/page-out and save; built as a separate ROM or loaded as an
  overlay only when a 64DD is present.
- Disk writes must not cause frame hitches: buffer evicted columns in RAM
  and write during saves or idle time; measure worst-case seeks.
- Requires the Expansion Pak (the 64DD shipped with it), so Q2 is settled
  for disk owners: 8 MB.
- Period hardware (Q7) and ares (Q7) both cover the 64DD. Verify ares 64DD
  emulation and libdragon 64DD support at N1; write our own driver if
  libdragon lacks one.

Earlier assessment kept for the record: the disk's only advantage is
capacity; it is slower than ROM/RAM and needs rarer hardware. The user
accepted that trade for an optional path.

## Q7. Authenticity rules — ANSWERED 2026-10-15

- **Tools:** modern tools accepted (libdragon, current GCC, host-side
  Python), as in DOScraft.
- **Hardware cutoff: period hardware only.** Only hardware Nintendo
  actually shipped, through about 2000 (Japan): the console, Expansion Pak,
  N64 Mouse (Dec 1999), Randnet keyboard (~2000) and period cartridge save
  chips (FlashRAM). No flashcart-only features: no SD-card saves, USB I/O
  or extra RAM, even as options.
- **ROM size: 64 MB maximum** (512 Mbit, the largest commercial size). For
  scale, the immutable part of DOScraft's V3 world medium is 24.4 MB
  (`86craft/DOScraft/research/edit-log-v3-2026-10-14.md`); the N64 format
  will differ, but one prebaked 1024x1024 world should fit with room left.
  Measure at N5.
- **Acceptance testing: ares only.** ares is the target platform, as
  DOScraft's 86Box VMs are. No real-hardware gate. Consequences:
  - ares's timing is the performance truth; no hardware calibration.
  - The Randnet keyboard must work in ares. If ares does not emulate it,
    a patched ares fork is needed (as DOScraft forked 86Box), pinned by
    revision and hash, with the patch tested separately. Check at N1.
  - Pin the ares version per release, like DOScraft pins its emulator.
