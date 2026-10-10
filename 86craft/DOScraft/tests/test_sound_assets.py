"""Native x86-Win32 assets parity against full original pinned Towns sound.c.

Only the PCM_WINDOW literal changes in the original reference, with its unique
replacement and hashes recorded in private test storage. Declaration-only hw.h
routes all port/IRQ access to bounded diagnostic mocks. No playback or emulator.
"""
import importlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported

ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
PCM_LITERAL = b'((volatile u8 *)0xC2200000)'
PCM_REPLACEMENT = b'(reference_pcm_window())'
FAKE_HW = '''/* Diagnostic declarations only: never emit hardware instructions. */
#ifndef TOWNS_AUDIO_REFERENCE_HW_H
#define TOWNS_AUDIO_REFERENCE_HW_H
#include "dos_compat.h"
void outb(u16 port,u8 value);
u8 inb(u16 port);
void cli(void);
void sti(void);
volatile u8 *reference_pcm_window(void);
#endif
'''


def audio_tool():
    return importlib.import_module('audio_assets')


def pinned_sound():
    provenance = json.loads((imported.VENDOR / 'MANIFEST.json').read_bytes())
    row = next(item for item in provenance['files'] if item['path'] == 'src/sound.c')
    data = (imported.VENDOR / row['path']).read_bytes()
    if (provenance['source_commit'] != imported.PINNED_COMMIT or
            imported.sha256(data) != row['sha256'] or len(data) != row['size']):
        raise AssertionError('Pinned original sound.c provenance mismatch')
    return data


def reference_input(directory):
    original = pinned_sound()
    if original.count(PCM_LITERAL) != 1:
        raise AssertionError('Expected exactly one original PCM_WINDOW literal')
    transformed = original.replace(PCM_LITERAL, PCM_REPLACEMENT, 1)
    source = directory / 'reference_original_sound.c'
    source.write_bytes(transformed)
    record = {
        'source': 'src/sound.c', 'source_commit': imported.PINNED_COMMIT,
        'source_sha256': imported.sha256(original),
        'staged_sha256': imported.sha256(transformed),
        'transformations': [{'operation': 'replace', 'before': PCM_LITERAL.decode(),
                             'after': PCM_REPLACEMENT.decode(), 'count': 1}],
    }
    (directory / 'reference_input.json').write_text(json.dumps(record, indent=2))
    (directory / 'hw.h').write_text(FAKE_HW)
    return source, record


def stage_verified(directory):
    report = audio_tool().stage(directory)
    record = report['audio_selection']
    original = pinned_sound()
    selections = record['selections']
    if any(not (0 <= start < end <= len(original)) for start, end in selections):
        raise AssertionError('Invalid pinned audio selection bounds')
    replay = b''.join(original[slice(*selections[part['source']])]
                      if 'source' in part else part['literal'].encode('ascii')
                      for part in record['assembly'])
    expected = [{'operation': 'replace', 'before': 'static int synthTop,synthBank=-1;',
                 'after': 'static int synthTop;', 'count': 1}]
    if record['transformations'] != expected:
        raise AssertionError('Unexpected synthesis/score transformation')
    for step in record['transformations']:
        before, after = step['before'].encode(), step['after'].encode()
        if replay.count(before) != step['count']:
            raise AssertionError('Audio transformation is not uniquely replayable')
        replay = replay.replace(before, after, step['count'])
    staged = (directory / record['staged']).read_bytes()
    if (report['source_commit'] != imported.PINNED_COMMIT or replay != staged or
            record['source'] != 'src/sound.c' or
            record['staged'] != 'src/towns_audio_assets.inc' or
            record['source_sha256'] != imported.sha256(original) or
            record['staged_sha256'] != imported.sha256(staged) or
            record['transformations_sha256'] != imported.sha256(
                imported.canonical_json(record['transformations']))):
        raise AssertionError('Pinned audio source selection provenance mismatch')
    return report


def run_command(command, timeout):
    result = subprocess.run(list(map(str, command)), capture_output=True,
                            text=True, errors='replace', timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


def build_native_test(directory):
    if sys.platform != 'win32' or not ZIG.is_file():
        raise RuntimeError('Existing native x86-Win32 Zig toolchain unavailable')
    stage_verified(directory)
    reference_input(directory)
    staged = directory / 'src'
    tables = staged / 'tables.c'
    run_command([sys.executable, directory / 'tools/gentables.py', tables], 20)
    flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2',
             '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I', directory,
             '-I', staged, '-I', ROOT / 'src/platform/dos']
    # Full original source compiles separately: no macro renames or shared
    # private synth/parser definitions with the production asset module.
    reference = directory / 'towns-reference.o'
    run_command([*flags, '-c', ROOT / 'tests/towns_sound_assets_reference.c',
                 '-o', reference], 120)
    executable = directory / 'sound-assets-test.exe'
    run_command([*flags, ROOT / 'tests/sound_assets_test.c', reference,
                 ROOT / 'src/platform/dos/sound_assets.c', staged / 'fmath.c',
                 tables, '-o', executable], 120)
    data = executable.read_bytes()
    if data[:2] != b'MZ':
        raise AssertionError('Expected native PE executable')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if (data[pe:pe + 4] != b'PE\0\0' or
            struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or
            struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
        raise AssertionError('Expected x86 PE32 native test executable')
    return executable


def native_reference_bytes(*, executable=None):
    """Return the full original-source diagnostic wire as immutable bytes.

    With no arguments, build/run in private temporary storage and clean up
    before returning. To reuse an active test setup, pass
    executable=SoundAssetsNativeTests.executable before its class cleanup.
    The reference_wire mode runs only the original Towns sound_init(), not the
    new dos_sound_assets_build(). It never reads or launches a guest VM.

    Main can compare its extracted SNDASSET.BIN directly with this return value.
    """
    with tempfile.TemporaryDirectory(prefix='doscraft-audio-reference-wire-') as name:
        directory = Path(name)
        if executable is None:
            executable = build_native_test(directory)
        output = directory / 'original-source-wire.bin'
        result = run_command([executable, 'reference_wire', output], 10)
        if 'PASS: pinned Towns audio assets reference_wire' not in result.stdout:
            raise AssertionError('Original-source wire helper did not finish')
        data = output.read_bytes()
        if len(data) < 180:
            raise AssertionError('Truncated original-source wire')
        magic, schema, wave_bytes, count, ticks = struct.unpack_from('<5I', data)
        if ((magic, schema) != (0x41534344, 1) or
                not (0 < wave_bytes <= 65536 and 0 < count <= 640 and 0 < ticks <= 65535) or
                len(data) != 180 + wave_bytes + count * 4):
            raise AssertionError('Invalid original-source diagnostic wire bounds')
        return data


class SoundAssetsProvenanceTests(unittest.TestCase):
    def test_pinned_audio_selection_is_exact_and_replayable(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-audio-stage-') as name:
            stage_verified(Path(name))

    def test_full_reference_has_one_recorded_unique_window_replacement(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-audio-reference-') as name:
            directory = Path(name)
            source, record = reference_input(directory)
            original = pinned_sound()
            self.assertEqual(source.read_bytes(), original.replace(PCM_LITERAL, PCM_REPLACEMENT, 1))
            self.assertEqual(source.read_bytes().replace(PCM_REPLACEMENT, PCM_LITERAL, 1), original)
            self.assertEqual(json.loads((directory / 'reference_input.json').read_text()), record)
            self.assertNotIn('__asm__', (directory / 'hw.h').read_text())

    def test_changed_pinned_sound_source_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-audio-corrupt-') as name:
            vendor = Path(name) / 'vendor'
            shutil.copytree(imported.VENDOR, vendor)
            source = vendor / 'src/sound.c'
            source.write_bytes(b'/* altered */\n' + source.read_bytes())
            with self.assertRaisesRegex(ValueError, 'provenance mismatch'):
                audio_tool().stage(Path(name) / 'stage', vendor=vendor)
            self.assertFalse((Path(name) / 'stage/src').exists())


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(),
                     'existing native x86-Win32 Zig toolchain unavailable')
class SoundAssetsNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='doscraft-audio-native-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.executable = build_native_test(cls.directory)

    run_command = staticmethod(run_command)

    def run_case(self, case, *arguments):
        result = self.run_command([self.executable, case, *arguments], 10)
        self.assertIn(f'PASS: pinned Towns audio assets {case}', result.stdout)
        return result.stdout

    def test_before_build_getters(self):
        self.run_case('before_build')

    def test_invalid_ids_and_optional_getter_outputs(self):
        self.run_case('invalid_ids')

    def test_every_unsigned_wave_byte_descriptor_and_merged_score_event(self):
        self.run_case('parity')

    def test_repeat_builds_restore_seed_score_and_identical_wire(self):
        self.run_case('repeat_builds')

    def test_bounded_little_endian_diagnostic_wire_matches_reference(self):
        assets = self.directory / 'assets-wire.bin'
        reference = self.directory / 'reference-wire.bin'
        self.run_case('wire', assets, reference)
        wire = assets.read_bytes()
        self.assertEqual(wire, reference.read_bytes())
        self.assertEqual(wire, native_reference_bytes(executable=self.executable))
        magic, schema, wave_bytes, count, ticks = struct.unpack_from('<5I', wire)
        self.assertEqual((magic, schema), (0x41534344, 1))
        self.assertTrue(0 < wave_bytes <= 65536)
        self.assertTrue(0 < count <= 640)
        self.assertTrue(0 < ticks <= 65535)
        self.assertEqual(len(wire), 20 + 10 * 16 + wave_bytes + count * 4)
        descriptors = [struct.unpack_from('<4I', wire, 20 + i * 16) for i in range(10)]
        wave = wire[180:180 + wave_bytes]
        self.assertEqual(wave[:3], b'\x80\x80\x80')
        for offset, frames, pitch, volume in descriptors:
            self.assertEqual(offset % 256, 0)
            self.assertTrue(frames > 0 and offset + frames < wave_bytes)
            self.assertTrue(0 < pitch <= 65535 and 0 <= volume <= 255)
            self.assertEqual(wave[offset + frames], 128)
        events = [struct.unpack_from('<HBB', wire, 180 + wave_bytes + i * 4)
                  for i in range(count)]
        self.assertEqual(events, sorted(events, key=lambda event: (event[0], event[2])))
        self.assertTrue(all(tick < ticks and note <= 127 and voice < 2
                            for tick, note, voice in events))


if __name__ == '__main__':
    unittest.main()
