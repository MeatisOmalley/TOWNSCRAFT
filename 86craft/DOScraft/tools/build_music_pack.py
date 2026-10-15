"""Convert user-supplied Standard MIDI Files into an OPL2 music pack (MUSIC.PAK).

The DOS game plays the pack on the Sound Blaster/AdLib OPL2 FM chip (9 two-
operator channels); see src/platform/dos/opl_music.c. Everything that needs
judgment happens here, offline, so the game only replays timed register work:

- Standard MIDI Files, format 0 or 1, with the tempo map applied; timing is
  quantized to the game's 100 Hz timer.
- Voices are allocated to the 9 OPL2 channels ahead of time. When more notes
  sound than there are channels, the oldest note is taken over ("steal").
- The sustain pedal (CC 64) holds note releases. On channels without any
  pedal data, notes on fading instruments (piano, electric piano, bell,
  pluck) ring for at least --hold seconds (default 1), but never past the
  next strike of the same key; many MIDI files have clipped, staccato notes.
  Sustained instruments keep their written lengths.
- Volume (CC 7), expression (CC 11) and velocity become carrier attenuation
  in 0.75 dB steps.
- General MIDI programs map to a few FM patches stored in the pack, so patches
  can be retuned without rebuilding the game. The drum channel (MIDI 10) is
  dropped; pitch bend and other controllers are ignored; notes outside the
  OPL2 range move by octaves. The report counts all of these.

Pack layout (little endian):
  header   8s magic, u32 version, flags (bit 0: keep the built-in Towns piece
           in the rotation), songs, patches, payload bytes, payload FNV-1a
  payload  patches x 12 bytes (modulator 20h/40h/60h/80h/E0h, carrier the
           same, C0h feedback/connection, pad); songs x 32 bytes (20-byte
           name, event offset, event count, length in ticks); events x 5
           bytes (u16 ticks since the previous event, u8 type<<4|channel,
           u8 a, u8 b). Types: 0 note off, 1 note on (a note, b attenuation),
           2 patch (a index), 3 wait only.
"""
import argparse
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
MIDI_DIR = ROOT / 'assets/music/midi'
MAGIC = b'DCMUS01\0'
VERSION = 1
FLAG_BUILTIN = 1
TICK_HZ = 100
CHANNELS = 9
MAX_SONGS = 32
MAX_EVENTS = 65535
NOTE_OFF, NOTE_ON, PATCH, WAIT = range(4)
# Two-operator patches: modulator then carrier registers 20h, 40h, 60h, 80h,
# E0h, then C0h. 20h bit 5 holds the note at its sustain level while keyed.
PATCHES = (
    # Piano carrier: sustain level -6 dB, release 4 (about 2 s to -40 dB). These
    # patches do not hold at the sustain level, so the release rate also sets
    # how long a held note rings.
    ('piano',  (0x01, 0x18, 0xF2, 0x54, 0x00), (0x01, 0x00, 0xF2, 0x24, 0x00), 0x06),
    ('epiano', (0x01, 0x1A, 0xF3, 0x35, 0x00), (0x01, 0x04, 0xF2, 0x24, 0x00), 0x06),
    ('bell',   (0x07, 0x18, 0xF5, 0x55, 0x00), (0x01, 0x00, 0xF3, 0x45, 0x00), 0x02),
    ('pad',    (0x21, 0x1E, 0x62, 0x14, 0x00), (0x21, 0x00, 0x52, 0x15, 0x00), 0x08),
    ('bass',   (0x01, 0x14, 0xF4, 0x56, 0x00), (0x01, 0x00, 0xF5, 0x58, 0x00), 0x06),
    ('organ',  (0x22, 0x0A, 0xF0, 0x0F, 0x00), (0x21, 0x00, 0xF0, 0x0F, 0x00), 0x01),
    ('pluck',  (0x01, 0x1C, 0xF6, 0x57, 0x00), (0x01, 0x00, 0xF6, 0x58, 0x00), 0x06),
    ('lead',   (0x21, 0x16, 0xF1, 0x16, 0x00), (0x21, 0x00, 0xF1, 0x16, 0x00), 0x0A),
)
PATCH_INDEX = {name: i for i, (name, *_rest) in enumerate(PATCHES)}
# Instruments that fade on their own, like a struck or plucked string. Without
# pedal data, their short notes ring for at least DEFAULT_HOLD seconds.
DECAYING = {'piano', 'epiano', 'bell', 'pluck'}
DEFAULT_HOLD = 1.0


def program_patch(program):
    """General MIDI program (0-127) to a pack patch index."""
    ranges = ((3, 'piano'), (5, 'epiano'), (7, 'pluck'), (15, 'bell'), (23, 'organ'),
              (31, 'pluck'), (39, 'bass'), (55, 'pad'), (87, 'lead'), (103, 'pad'),
              (111, 'pluck'), (119, 'bell'), (127, 'pad'))
    return PATCH_INDEX[next(name for top, name in ranges if program <= top)]


def checksum(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def read_varlen(data, at):
    value = 0
    for _ in range(4):
        byte = data[at]
        at += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, at
    raise ValueError('Variable-length quantity too long')


def parse_midi(data):
    """(division, events): events are (tick, order, kind, channel, a, b) with
    kind 'on'/'off'/'cc'/'program'/'tempo'/'bend'; order keeps file order."""
    if data[:4] != b'MThd':
        raise ValueError('Not a Standard MIDI File')
    length, fmt, ntracks, division = struct.unpack_from('>IHHH', data, 4)
    if fmt not in (0, 1):
        raise ValueError(f'MIDI format {fmt} is not supported (use 0 or 1)')
    at = 8 + length
    events = []
    order = 0
    for _ in range(ntracks):
        if data[at:at + 4] != b'MTrk':
            raise ValueError('Missing MIDI track chunk')
        size = struct.unpack_from('>I', data, at + 4)[0]
        at += 8
        end = at + size
        tick = 0
        status = None
        while at < end:
            delta, at = read_varlen(data, at)
            tick += delta
            byte = data[at]
            if byte & 0x80:
                at += 1
                if byte < 0xF0:
                    status = byte
            elif status is None:
                raise ValueError('Running status before any status byte')
            else:
                byte = status
            if byte == 0xFF:
                kind = data[at]
                length, at = read_varlen(data, at + 1)
                if kind == 0x51 and length == 3:
                    events.append((tick, order, 'tempo', 0, int.from_bytes(data[at:at + 3], 'big'), 0))
                    order += 1
                at += length
                if kind == 0x2F:
                    break
                continue
            if byte in (0xF0, 0xF7):
                length, at = read_varlen(data, at)
                at += length
                continue
            kind, channel = byte & 0xF0, byte & 0x0F
            if kind in (0xC0, 0xD0):
                a, b = data[at], 0
                at += 1
            else:
                a, b = data[at], data[at + 1]
                at += 2
            name = {0x80: 'off', 0x90: 'on' if b else 'off', 0xB0: 'cc', 0xC0: 'program',
                    0xE0: 'bend'}.get(kind)
            if name:
                events.append((tick, order, name, channel, a, b))
                order += 1
        at = end
    return division, events


def seconds_function(division, events):
    """Tick -> seconds with the file's tempo map."""
    if division & 0x8000:
        fps = 256 - (division >> 8)
        per_frame = division & 0xFF
        return lambda tick: tick / (fps * per_frame)
    changes = sorted((e[0], e[4]) for e in events if e[2] == 'tempo')
    points = [(0, 0.0, 500000)]
    for tick, tempo in changes:
        last_tick, last_seconds, last_tempo = points[-1]
        seconds = last_seconds + (tick - last_tick) * last_tempo / 1e6 / division
        if tick == last_tick:
            points[-1] = (tick, last_seconds, tempo)
        else:
            points.append((tick, seconds, tempo))

    def convert(tick):
        base = points[0]
        for point in points:
            if point[0] <= tick:
                base = point
            else:
                break
        return base[1] + (tick - base[0]) * base[2] / 1e6 / division
    return convert


def attenuation(velocity, volume, expression):
    level = velocity / 127 * volume / 127 * expression / 127
    if level <= 0:
        return 63
    return max(0, min(63, round(-20 * math.log10(level) / 0.75)))


def read_notes(data):
    """MIDI bytes -> (notes, info). Notes are dicts with on/off in game ticks,
    MIDI channel, key, attenuation and patch; the pedal (CC 64) is applied."""
    division, midi = parse_midi(data)
    if not division:
        raise ValueError('MIDI division is zero')
    to_seconds = seconds_function(division, midi)
    midi.sort(key=lambda e: (e[0], 0 if e[2] == 'off' else 1, e[1]))
    info = dict(drum_notes=0, pitch_bends=0, pedal_channels=set())
    program = [0] * 16
    volume = [100] * 16
    expression = [127] * 16
    pedal = [False] * 16
    held = [set() for _ in range(16)]      # Keys released while the pedal is down
    active = {}                             # (midi channel, key) -> sounding note
    notes = []
    tick = 0

    def close(key, at):
        note = active.pop(key, None)
        if note is not None:
            note['off'] = at

    for tick_midi, _order, kind, mch, a, b in midi:
        tick = round(to_seconds(tick_midi) * TICK_HZ)
        if kind == 'program':
            program[mch] = a
        elif kind == 'bend':
            info['pitch_bends'] += 1
        elif kind == 'cc':
            if a == 7:
                volume[mch] = b
            elif a == 11:
                expression[mch] = b
            elif a == 64:
                info['pedal_channels'].add(mch)
                pedal[mch] = b >= 64
                if not pedal[mch]:
                    for key in sorted(held[mch]):
                        close((mch, key), tick)
                    held[mch].clear()
            elif a in (120, 123):               # All sound / notes off
                for key in [k for k in active if k[0] == mch]:
                    close(key, tick)
                held[mch].clear()
        elif mch == 9:
            info['drum_notes'] += kind == 'on'
        elif kind == 'off':
            if pedal[mch] and (mch, a) in active:
                held[mch].add(a)
            else:
                close((mch, a), tick)
        elif kind == 'on':
            held[mch].discard(a)
            close((mch, a), tick)               # Striking a sounding key restarts it
            note = dict(on=tick, off=None, channel=mch, key=a, patch=program_patch(program[mch]),
                        attenuation=attenuation(b, volume[mch], expression[mch]))
            active[(mch, a)] = note
            notes.append(note)
    for key in list(active):
        close(key, tick)
    for note in notes:
        note['off'] = max(note['off'], note['on'] + 1)
    return notes, info


def apply_hold(notes, pedal_channels, hold):
    """Let short notes on decaying instruments ring for at least hold seconds,
    on channels without pedal data, never past the next strike of that key.
    Returns how many notes were lengthened."""
    hold_ticks = round(hold * TICK_HZ)
    if hold_ticks <= 0:
        return 0
    next_strike = {}
    longer = 0
    for note in sorted(notes, key=lambda n: n['on'], reverse=True):
        key = (note['channel'], note['key'])
        if (note['channel'] not in pedal_channels and PATCHES[note['patch']][0] in DECAYING and
                note['off'] < note['on'] + hold_ticks):
            target = min(note['on'] + hold_ticks, next_strike.get(key, note['on'] + hold_ticks))
            if target > note['off']:
                note['off'] = target
                longer += 1
        next_strike[key] = note['on']
    return longer


def convert_song(data, hold=DEFAULT_HOLD):
    """MIDI bytes -> (events [(tick, type, channel, a, b)], report)."""
    notes, info = read_notes(data)
    report = dict(notes=len(notes), max_polyphony=0, steals=0, drum_notes=info['drum_notes'],
                  octave_moves=0, pitch_bends=info['pitch_bends'], patches_used=set(),
                  pedal=bool(info['pedal_channels']),
                  sustained_notes=apply_hold(notes, info['pedal_channels'], hold))
    # Releases sort before strikes at the same tick, so voices free up first.
    timeline = sorted([(n['on'], 1, i) for i, n in enumerate(notes)] +
                      [(n['off'], 0, i) for i, n in enumerate(notes)])
    voice = {}                              # note index -> OPL channel
    owner = [None] * CHANNELS               # OPL channel -> note index
    started = [0] * CHANNELS
    last_used = [0] * CHANNELS
    patch = [None] * CHANNELS
    out = []
    for serial, (tick, strike, index) in enumerate(timeline, 1):
        if not strike:
            ch = voice.pop(index, None)
            if ch is not None:
                owner[ch] = None
                last_used[ch] = serial
                out.append((tick, NOTE_OFF, ch, 0, 0))
            continue
        note = notes[index]
        want = note['patch']
        free = [c for c in range(CHANNELS) if owner[c] is None]
        if free:
            # Prefer a free channel that already has this patch, then the one
            # idle longest (its release tail has faded most).
            ch = min(free, key=lambda c: (patch[c] != want, last_used[c]))
        else:
            ch = min(range(CHANNELS), key=lambda c: started[c])
            report['steals'] += 1
            voice.pop(owner[ch], None)
        key = note['key']
        while key < 12:
            key += 12
        while key > 107:
            key -= 12
        if key != note['key']:
            report['octave_moves'] += 1
        if patch[ch] != want:
            out.append((tick, PATCH, ch, want, 0))
            patch[ch] = want
        report['patches_used'].add(PATCHES[want][0])
        out.append((tick, NOTE_ON, ch, key, note['attenuation']))
        owner[ch] = index
        voice[index] = ch
        started[ch] = serial
        report['max_polyphony'] = max(report['max_polyphony'], len(voice))
    report['patches_used'] = sorted(report['patches_used'])
    report['seconds'] = round((out[-1][0] if out else 0) / TICK_HZ, 2)
    return out, report


def encode_events(events):
    """Timed events -> 5-byte records with u16 deltas (long gaps get waits)."""
    data = bytearray()
    previous = 0
    count = 0
    for tick, kind, ch, a, b in events:
        delta = tick - previous
        while delta > 0xFFFF:
            data += struct.pack('<HBBB', 0xFFFF, WAIT << 4, 0, 0)
            delta -= 0xFFFF
            count += 1
        data += struct.pack('<HBBB', delta, (kind << 4) | ch, a, b)
        previous = tick
        count += 1
    return bytes(data), count


def build_pack(songs, include_builtin=True, hold=DEFAULT_HOLD):
    """songs: [(name, midi bytes)] -> (pack bytes, reports)."""
    if not songs or len(songs) > MAX_SONGS:
        raise ValueError(f'Need 1 to {MAX_SONGS} songs')
    patches = b''.join(bytes(mod) + bytes(car) + bytes((c0, 0)) for _name, mod, car, c0 in PATCHES)
    directory = bytearray()
    events = bytearray()
    reports = []
    for name, data in songs:
        timed, report = convert_song(data, hold)
        encoded, count = encode_events(timed)
        if not count or count > MAX_EVENTS:
            raise ValueError(f'{name}: {count} events (1 to {MAX_EVENTS} allowed)')
        label = name.encode('ascii', 'replace')[:19]
        directory += struct.pack('<20sIII', label, len(events), count, timed[-1][0])
        events += encoded
        reports.append(dict(name=name, events=count, bytes=len(encoded), **report))
    payload = patches + bytes(directory) + bytes(events)
    header = struct.pack('<8s6I', MAGIC, VERSION, FLAG_BUILTIN if include_builtin else 0,
                         len(songs), len(PATCHES), len(payload), checksum(payload))
    return header + payload, reports


def build(output, midi_dir=MIDI_DIR, include_builtin=True, hold=DEFAULT_HOLD):
    """Build MUSIC.PAK from every .mid in midi_dir; None when there are none."""
    files = sorted(p for p in Path(midi_dir).glob('*') if p.suffix.lower() in ('.mid', '.midi'))
    if not files:
        return None
    pack, reports = build_pack([(p.stem, p.read_bytes()) for p in files], include_builtin, hold)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(pack)
    from hashlib import sha256
    report = dict(format='DCMUS01', bytes=len(pack), sha256=sha256(pack).hexdigest(),
                  builtin_towns_piece=include_builtin, songs=reports,
                  patches=[name for name, *_rest in PATCHES])
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--input', type=Path, default=MIDI_DIR)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/assets/music/MUSIC.PAK')
    parser.add_argument('--no-builtin', action='store_true', help='Leave the Towns piece out of the rotation')
    parser.add_argument('--hold', type=float, default=DEFAULT_HOLD,
                        help=f'Minimum ring time in seconds for fading instruments without pedal data '
                             f'(default {DEFAULT_HOLD}; 0 keeps written lengths)')
    args = parser.parse_args()
    if args.hold < 0:
        parser.error('--hold cannot be negative')
    report = build(args.output, args.input, not args.no_builtin, args.hold)
    if report is None:
        parser.error(f'No .mid files in {args.input}')
    for song in report['songs']:
        notes = [f'{song["max_polyphony"]} voices max', 'pedal' if song['pedal'] else 'no pedal data']
        for key, label in (('sustained_notes', 'short notes held longer'),
                           ('steals', 'notes cut for polyphony'), ('drum_notes', 'drum notes dropped'),
                           ('octave_moves', 'notes moved by octaves'), ('pitch_bends', 'pitch bends ignored')):
            if song[key]:
                notes.append(f'{song[key]} {label}')
        print(f'{song["name"]}: {song["seconds"]} s, {song["notes"]} notes, '
              f'{", ".join(song["patches_used"])}; ' + '; '.join(notes))
    print(f'{args.output}: {report["bytes"]} bytes, {len(report["songs"])} song(s)'
          + (' plus the Towns piece' if report['builtin_towns_piece'] else ''))


if __name__ == '__main__':
    main()
