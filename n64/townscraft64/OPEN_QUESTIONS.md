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

Recommendation: target 4 MB for Phase A; Expansion Pak as a detected bonus
mode in Phase B.

## Q3. SDK (blocks N1)

- **libdragon** (open source, actively developed). Recommended.
- libultra (Nintendo's official SDK) is only available via leaks; not
  recommended for legal reasons.

Also: whether to use a community 3D layer on top of libdragon (its OpenGL
port, or tiny3d) or write our own RSP/RDP path. Decide after N1 with
measurements.

## Q4. Visual style (blocks N3)

The RDP can do bilinear filtering, perspective correction and smooth
lighting. Keep the point-sampled, per-face-lit look of the Towns version
for parity, or take the N64 look? Recommendation: point sampling and
per-face light in Phase A (parity), filtering/smooth light as Phase B
options.

## Q5. Controller mapping (blocks N3)

The game uses WASD + mouse/arrows and many keys. A proposal to review:

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

Alternative: stick moves, C-buttons look (Turok-style). Optional N64 Mouse
support for look.

## Q6. Save model (blocks N5)

Cartridge saves cap at 128 KB (FlashRAM). Accept "world in ROM, edits as a
compressed delta, limited edit volume per save"? Or a different approach,
for example a smaller world or Controller Pak overflow. This changes save
behavior relative to the Towns and DOS versions, so it needs approval
before Phase A treats it as parity.

## Q7. Authenticity rules

DOScraft fixed hardware cutoffs (June 1990, 1992). For the N64:

- Modern tools (libdragon, GCC) accepted, as in DOScraft?
- Must the ROM fit period cartridge sizes (≤ 64 MB) and a period save chip?
- Is real-hardware testing on a flashcart required for acceptance, or is
  ares enough?
