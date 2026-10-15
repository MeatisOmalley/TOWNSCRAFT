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

## Q2. Base 4 MB or Expansion Pak? (blocks N3)

- 4 MB is every console and the honest 1996 target.
- 8 MB (1998 accessory, needed by Majora's Mask) roughly doubles world and
  mesh space.

Status 2026-10-15: **deferred**; the user is unsure and will discuss it
later. With DOScraft as the reference (Q1) the world itself lives in ROM, so
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

64DD: considered and **not planned**. Its only advantage is capacity
(unlimited edits, several worlds); it is slower (disk seeks on page-out),
needs a 64DD plus the Expansion Pak, and adds a driver. Revisit only if
measured play hits the cap or several worlds per cartridge are wanted. More
RAM comes from the Expansion Pak (Q2), and new seeds could be generated
on demand per column without writable storage.

Measure at N5: real edit volume from play sessions against the budget.

## Q7. Authenticity rules

DOScraft fixed hardware cutoffs (June 1990, 1992). For the N64:

- Modern tools (libdragon, GCC) accepted, as in DOScraft?
- Must the ROM fit period cartridge sizes (≤ 64 MB) and a period save chip?
- Is real-hardware testing on a flashcart required for acceptance, or is
  ares enough?
