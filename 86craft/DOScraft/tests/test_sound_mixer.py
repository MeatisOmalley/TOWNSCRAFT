"""Sample-for-sample foreground mixer tests, with a Python integer oracle.

The oracle uses elapsed-frame multiplication rather than the mixer's phase
accumulator, Python unbounded ints, and independent voice replacement state.
The real asset test reads the separately compiled pinned Towns reference wire.
All generated sources/binaries/commands stay in private temporary directories.
"""
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
DEFAULT_RATE = 1000000 // 91
MAX_FRAMES = 65536
INT_MIN, INT_MAX, UINT_MAX = -(1 << 31), (1 << 31) - 1, (1 << 32) - 1


def command_run(command, timeout=60):
    result = subprocess.run(list(map(str, command)), capture_output=True,
                            text=True, errors='replace', timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


def compiler_flags():
    if sys.platform == 'win32' and ZIG.is_file():
        return [ZIG, 'cc', '-target', 'x86-windows-gnu']
    compiler = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
    if compiler:
        return [compiler]
    raise unittest.SkipTest('Native C compiler unavailable')


def fixture_assets():
    wave = bytearray(((i * 73 + i // 7 * 19) ^ (i >> 3)) & 255 for i in range(65536))
    wave[:5] = bytes([255, 0, 200, 80, 129])
    wave[64:66] = bytes([255, 0])
    samples = [(0, 5, 256, 255), (16, 7, 128, 170), (32, 3, 65535, 255),
               (48, 9, 1, 120), (64, 1, 256, 255), (65, 1, 256, 255),
               (0, 65536, 8192, 255), (0, 0, 256, 255), (UINT_MAX, 2, 256, 255),
               (1, UINT_MAX, 256, 255), (0, 5, 0, 255), (0, 5, 256, 0)]
    return bytes(wave), samples


class Oracle:
    def __init__(self, wave, samples):
        self.wave, self.samples = wave, samples
        self.ready = False
        self.rate = 0
        self.stop()

    def stop(self):
        self.oneshots = [None] * 7
        self.loop_voice = None
        self.cursor = 0

    def descriptor(self, ident, volume):
        if not self.rate or not 0 <= volume <= 255 or not 0 <= ident < len(self.samples):
            return None
        offset, length, pitch, _ = desc = self.samples[ident]
        if length <= 0 or pitch <= 0 or offset >= len(self.wave) or offset + length > len(self.wave):
            return None
        return desc

    def voice(self, ident, pitch, volume, pan):
        offset, length, base_pitch, _ = self.samples[ident]
        fd = min(65535, max(64, (base_pitch * 8 * pitch) // 256))
        return dict(ident=ident, offset=offset, length=length, elapsed=0,
                    step=(20833 * fd * 65536) // (2048 * self.rate),
                    volume=volume, fold=30 - abs(min(15, max(-15, pan))))

    def render(self, frames):
        output = bytearray()
        for _ in range(frames):
            total = 0
            for voice in self.oneshots + [self.loop_voice]:
                if voice is None:
                    continue
                # Time from the voice's start, not a repeated phase addition.
                phase = voice['elapsed'] * voice['step']
                if voice is self.loop_voice:
                    phase %= voice['length'] * 65536
                index = phase // 65536
                if index < voice['length']:
                    value = self.wave[voice['offset'] + index] - 128
                    numerator = value * voice['volume'] * voice['fold']
                    magnitude = abs(numerator) // (256 * 30)
                    total += -magnitude if numerator < 0 else magnitude
                voice['elapsed'] += 1
            output.append(min(255, max(0, total + 128)))
        return bytes(output)

    def run(self, commands):
        output = bytearray()
        for command in commands:
            op, *args = command
            if op == 'build':
                self.ready = True
            elif op == 'init':
                rate, status = args
                self.stop()
                self.rate = rate if rate and self.ready else 0
                assert status == (0 if self.rate else -1)
            elif op == 'stop':
                self.stop()
            elif op in ('play', 'loop'):
                if op == 'play':
                    ident, pitch, volume, pan = args
                else:
                    ident, volume = args
                    pitch, pan = 256, 0
                desc = self.descriptor(ident, volume)
                if desc is None:
                    continue
                scaled = volume * desc[3] // 256
                if op == 'play':
                    if scaled:
                        self.oneshots[self.cursor] = self.voice(ident, pitch, scaled, pan)
                        self.cursor = (self.cursor + 1) % 7
                elif not scaled:
                    self.loop_voice = None
                elif self.loop_voice and self.loop_voice['ident'] == ident:
                    self.loop_voice['volume'] = scaled
                else:
                    self.loop_voice = self.voice(ident, pitch, scaled, pan)
            elif op == 'render':
                output.extend(self.render(args[0]))
            elif op in ('null', 'reject'):
                pass  # Invalid/empty renders cannot consume time.
            else:
                raise AssertionError(op)
        return bytes(output)


class SoundMixerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='doscraft-mixer-native-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.flags = [*compiler_flags(), '-std=c99', '-O0', '-Wall', '-Wextra', '-Werror',
                     '-I', ROOT / 'src/platform/dos']
        cls.executable = cls.directory / 'mixer-test.exe'
        command_run([*cls.flags, ROOT / 'tests/sound_mixer_test.c',
                     ROOT / 'src/platform/dos/sound_mixer.c', '-o', cls.executable])
        cls.wave, cls.samples = fixture_assets()

    def native(self, commands, executable=None):
        script, pcm = self.directory / 'commands.txt', self.directory / 'output.bin'
        script.write_text(''.join(' '.join(map(str, command)) + '\n' for command in commands))
        result = command_run([executable or self.executable, script, pcm], 30)
        self.assertIn('PASS: foreground sound mixer driver', result.stdout)
        return pcm.read_bytes()

    def compare(self, commands, wave=None, samples=None, executable=None):
        expected = Oracle(self.wave if wave is None else wave,
                          self.samples if samples is None else samples).run(commands)
        actual = self.native(commands, executable)
        self.assertEqual(actual, expected)
        return actual

    @staticmethod
    def startup(rate=DEFAULT_RATE):
        return [('build',), ('init', rate, 0)]

    def test_uninitialized_failed_init_and_bounded_render(self):
        commands = [('play', 0, 256, 255, 0), ('loop', 0, 255), ('render', 9),
                    ('init', DEFAULT_RATE, -1), ('render', 9), ('build',),
                    ('init', 0, -1), ('render', 9), ('init', DEFAULT_RATE, 0),
                    ('loop', 0, 255), ('render', 4), ('null', 0, 0),
                    ('null', 1, -1), ('null', UINT_MAX, -1),
                    ('reject', MAX_FRAMES + 1), ('reject', UINT_MAX),
                    ('render', 0), ('render', 11), ('init', 0, -1), ('render', 9)]
        self.compare(commands)
        self.compare(self.startup() + [('render', MAX_FRAMES)])

    def test_rates_pitches_and_pan_sample_for_sample(self):
        for rate in (1, DEFAULT_RATE, 8000, 11025, 20833, 44100, 65535, UINT_MAX):
            with self.subTest(rate=rate):
                commands = self.startup(rate)
                for ident in (0, 1, 2, 3, 6):
                    for pitch in (INT_MIN, -1, 0, 1, 64, 128, 256, 511, 8192, INT_MAX):
                        for pan in (INT_MIN, -15, -7, 0, 9, 15, INT_MAX):
                            commands += [('stop',), ('play', ident, pitch, 255, pan),
                                         ('render', 13)]
                self.compare(commands)

    def test_isolated_amplitude_pan_fold_and_signed_rounding(self):
        # These explicit values detect any blanket /8 attenuation, as well as
        # unsigned centering and different negative-side division rounding.
        commands = self.startup(20833)
        for ident, pan in ((4, 0), (5, 0), (4, -15), (4, 15), (5, 15), (4, 7)):
            commands += [('stop',), ('play', ident, 256, 255, pan), ('render', 2)]
        self.assertEqual(self.compare(commands),
                         bytes([254, 128, 1, 128, 191, 128, 191, 128, 65, 128, 224, 128]))
        commands = self.startup(20833)
        for volume in (0, 1, 2, 17, 128, 255):
            commands += [('stop',), ('play', 0, 256, volume, 0), ('render', 6)]
        self.compare(commands)

    def test_overlap_clips_after_signed_sum_with_no_headroom_division(self):
        commands = self.startup(20833)
        for first, second in ((4, 4), (5, 5), (4, 5), (4, 1)):
            commands += [('stop',), ('play', first, 256, 255, 0),
                         ('play', second, 256, 255, 0), ('render', 1)]
        pcm = self.compare(commands)
        self.assertEqual(pcm[:3], bytes([255, 0, 127]))
        self.compare(self.startup(20833) + [('play', 4, 256, 255, 0)] * 7 +
                     [('loop', 5, 255), ('render', 3)])

    def test_round_robin_replacement_and_ignored_requests_do_not_allocate_voices(self):
        baseline = self.startup(44100)
        for ident, pitch, vol, pan in ((6, 256, 36, 0), (0, 64, 77, -15),
                                      (1, 90, 90, 8), (3, 256, 180, -6),
                                      (0, 32, 33, 1), (1, 64, 55, 0),
                                      (6, 128, 71, 7)):
            baseline.append(('play', ident, pitch, vol, pan))
        baseline += [('loop', 1, 100), ('render', 3)]
        invalid = [('play', ident, 256, 255, 0)
                   for ident in (INT_MIN, -1, 7, 8, 9, 10, 12, INT_MAX)]
        invalid += [('play', 0, 256, vol, 0) for vol in (INT_MIN, -1, 0, 1, 256, INT_MAX)]
        invalid += [('play', 11, 256, 255, 0)]
        tail = [('play', 2, 1, 55, 0), ('render', 35),
                ('play', 6, 128, 100, 0), ('render', 20)]
        expected = self.compare(baseline + tail)
        self.assertEqual(self.compare(baseline + invalid + tail), expected)
        # Exercise several complete replacement cycles while keeping the eighth
        # loop audible and unaffected by one-shot channel allocation.
        commands = baseline
        for i in range(29):
            commands += [('play', i % 7, 30 + i * 71, 20 + i * 5, i % 31 - 15),
                         ('render', i % 5)]
        self.compare(commands)

    def test_loop_same_id_update_retains_phase_change_restarts_and_stop(self):
        commands = self.startup(44100) + [('loop', 0, 255), ('render', 3),
                    ('loop', 0, 91), ('render', 9), ('loop', 0, 255), ('render', 4),
                    ('loop', 1, 220), ('render', 5), ('loop', 0, 255), ('render', 7)]
        for ident in (-1, 7, 8, 9, 10, 12, INT_MAX):
            commands += [('loop', ident, 0), ('loop', ident, 255), ('render', 2)]
        commands += [('loop', 0, -1), ('loop', 0, 256), ('render', 5),
                     ('play', 6, 128, 255, 0), ('loop', 0, 0), ('render', 5),
                     ('loop', 0, 255), ('render', 3), ('loop', 0, 1), ('render', 4),
                     ('loop', 0, 255), ('render', 3), ('stop',), ('render', 20)]
        self.assertEqual(self.compare(commands)[-20:], bytes([128]) * 20)
        # No reset on same-ID updates, even across many complete loop traversals.
        self.compare(self.startup() + [('loop', 1, 255), ('render', 600),
                                      ('loop', 1, 255), ('render', 600)])

    def test_loop_multiwrap_and_large_phase_step_and_end(self):
        for rate in (1, 2, 100, 20833):
            with self.subTest(rate=rate):
                commands = self.startup(rate)
                for ident in (0, 1, 2, 3, 6):
                    commands += [('stop',), ('loop', ident, 255), ('render', 4500),
                                 ('loop', ident, 128), ('render', 300)]
                commands += [('stop',), ('play', 6, INT_MAX, 255, 0),
                             ('render', 4500), ('render', 300)]
                self.compare(commands)
        # FD 65535, rate 1: Q16 step exceeds 2^32. A short one-shot must
        # terminate after one output sample instead of wrapping into its data.
        self.assertEqual(self.compare(self.startup(1) +
                         [('play', 4, INT_MAX, 255, 0), ('render', 4)]),
                         bytes([254, 128, 128, 128]))

    def test_chunking_invariance_including_maximum_chunks(self):
        setup = self.startup() + [('play', 6, 128, 177, -3), ('play', 0, 99, 255, 0),
                                ('play', 1, 511, 131, 14), ('loop', 2, 119)]
        whole = self.compare(setup + [('render', MAX_FRAMES), ('render', 4501)])
        rng = random.Random(8127)
        chunks, remaining = [], MAX_FRAMES + 4501
        while remaining:
            count = min(remaining, rng.randrange(1, 2300))
            chunks += [('render', 0), ('null', 0, 0), ('render', count)]
            remaining -= count
        self.assertEqual(self.compare(setup + chunks), whole)

    def test_repeated_init_resets_voices_rate_phase_and_cursor(self):
        commands = self.startup()
        reference = None
        for _ in range(5):
            sequence = [('init', DEFAULT_RATE, 0), ('play', 0, 99, 255, -4),
                        ('play', 6, 128, 190, 8), ('loop', 1, 123), ('render', 47)]
            standalone = self.compare(self.startup() + sequence)
            if reference is None:
                reference = standalone
            self.assertEqual(standalone, reference)
            commands += sequence + [('play', 6, 256, 255, 0)] * 8
        self.compare(commands)
        self.compare(self.startup() + [('loop', 6, 255), ('render', 11),
                     ('init', 20833, 0), ('render', 5), ('loop', 6, 255), ('render', 91),
                     ('stop',), ('build',), ('init', DEFAULT_RATE, 0), ('render', 3)])

    def test_deterministic_mixed_command_sequences(self):
        rng = random.Random(20833)
        commands = self.startup()
        for _ in range(1100):
            choice = rng.randrange(10)
            if choice < 4:
                commands.append(('play', rng.randrange(-2, 14),
                                 rng.choice([INT_MIN, 0, 1, 64, 256, 511, INT_MAX]),
                                 rng.choice([-1, 0, 1, 50, 128, 255, 256]),
                                 rng.randrange(-25, 26)))
            elif choice < 7:
                commands.append(('loop', rng.randrange(-2, 14),
                                 rng.choice([-1, 0, 1, 75, 200, 255, 256])))
            elif choice == 7:
                commands.append(('stop',))
            elif choice == 8:
                rate = rng.choice([0, 1, DEFAULT_RATE, 20833, UINT_MAX])
                commands.append(('init', rate, 0 if rate else -1))
            commands.append(('render', rng.randrange(20)))
        self.compare(commands)

    def test_real_committed_assets_against_pinned_reference_wave(self):
        if sys.platform != 'win32' or not ZIG.is_file():
            self.skipTest('Pinned x86-Win32 asset reference toolchain unavailable')
        import test_sound_assets as reference
        directory = self.directory / 'real-assets'
        directory.mkdir()
        reference_exe = reference.build_native_test(directory)
        wire = reference.native_reference_bytes(executable=reference_exe)
        _, _, wave_bytes, _, _ = struct.unpack_from('<5I', wire)
        samples = [struct.unpack_from('<4I', wire, 20 + i * 16) for i in range(10)]
        wave = wire[180:180 + wave_bytes]
        staged = directory / 'src'
        executable = directory / 'real-mixer.exe'
        command_run([*self.flags, '-DMIXER_REAL_ASSETS', '-I', staged,
                     ROOT / 'tests/sound_mixer_test.c', ROOT / 'src/platform/dos/sound_mixer.c',
                     ROOT / 'src/platform/dos/sound_assets.c', staged / 'fmath.c',
                     staged / 'tables.c', '-o', executable])
        commands = self.startup()
        for ident in range(10):
            for pitch in (0, 128, 256, 511, INT_MAX):
                commands += [('stop',), ('play', ident, pitch, 255, ident * 3 - 15),
                             ('render', 1200)]
        commands += [('stop',), ('loop', 9, 255), ('render', 17000),
                     ('loop', 9, 100), ('render', 4300)]
        for ident in range(18):
            commands += [('play', ident % 10, 90 + ident * 23, 255, ident - 9),
                         ('render', 43)]
        commands += [('stop',), ('render', 500)]
        self.compare(commands, wave, samples, executable)

    def test_production_module_has_no_allocator_hardware_or_gameplay_dependencies(self):
        obj = self.directory / 'mixer.o'
        command_run([*self.flags, '-fno-builtin', '-c',
                     ROOT / 'src/platform/dos/sound_mixer.c', '-o', obj])
        assembly = self.directory / 'mixer.s'
        command_run([*self.flags, '-fno-builtin', '-S',
                     ROOT / 'src/platform/dos/sound_mixer.c', '-o', assembly])
        source = (ROOT / 'src/platform/dos/sound_mixer.c').read_text()
        for forbidden in ('malloc', 'calloc', 'realloc', 'free(', 'g_sfxOn', 'g_musicOn',
                          'outb(', 'inb(', 'cli(', 'sti(', 'hw.h', '__asm'):
            self.assertNotIn(forbidden, source)
        text = assembly.read_text()
        self.assertNotRegex(text, r'\b(?:call\w*|jmp\w*)\s+_?(?:malloc|calloc|realloc|free)\b')


if __name__ == '__main__':
    unittest.main()
