"""Real immutable staged Towns mob codec; native x86 Win32, no DOS guest.

The C harness includes the full staged mobs.c for private fixture inspection,
as Towns' mob_regions_test.c does. Dependencies are real staged blocks/math/RNG;
bounded heap and explicitly unused game callbacks are diagnostic fixtures.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported

ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
CASES = ('roundtrip', 'empty_capacity', 'malformed_headers', 'malformed_records',
         'malformed_ranges', 'valid_boundaries', 'truncation')


def stage_verified(directory):
    report = imported.stage_sources(imported.VENDOR, directory)
    record = next(item for item in report['files'] if item['source'] == 'src/mobs.c')
    original = (imported.VENDOR / record['source']).read_bytes()
    staged = (directory / record['staged']).read_bytes()
    if (report['source_commit'] != imported.PINNED_COMMIT or
            record['transformations'] or original != staged or
            imported.sha256(staged) != record['source_sha256'] or
            record['source_sha256'] != record['staged_sha256']):
        raise AssertionError('Mob codec must be the byte-identical pinned source')
    return report


class MobCodecProvenanceTests(unittest.TestCase):
    def test_pinned_staging_preserves_entire_mobs_source(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-mobs-provenance-') as name:
            report = stage_verified(Path(name))
            self.assertEqual(report['source_commit'], imported.PINNED_COMMIT)


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(),
                     'existing native Win32 Zig toolchain unavailable')
class MobCodecNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='doscraft-mobs-native-')
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        stage_verified(directory)
        staged = directory / 'src'
        tables = staged / 'tables.c'
        cls.run_command([sys.executable, directory / 'tools/gentables.py', tables], 20)
        cls.executable = directory / 'mob-codec-test.exe'
        cls.run_command([
            ZIG, 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2',
            '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I', staged,
            ROOT / 'tests/mob_codec_test.c', staged / 'blocks.c',
            staged / 'fmath.c', staged / 'libc.c', tables, '-o', cls.executable,
        ], 120)
        # Verify PE32/i386, rather than trusting the host compiler's default ABI.
        data = cls.executable.read_bytes()
        if data[:2] != b'MZ':
            raise AssertionError('Native test executable is not PE')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if (data[pe:pe + 4] != b'PE\0\0' or
                struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or
                struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
            raise AssertionError('Native test executable must be x86 PE32')

    @staticmethod
    def run_command(command, timeout):
        result = subprocess.run(list(map(str, command)), capture_output=True,
                                text=True, errors='replace', timeout=timeout)
        if result.returncode:
            raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                                 f'{result.stdout}\n{result.stderr}')
        return result

    def run_case(self, name):
        self.assertIn(name, CASES)
        result = self.run_command([self.executable, name], 10)
        self.assertIn(f'PASS: pinned Towns mobs codec {name}', result.stdout)
        return result.stdout

    def test_active_dormant_roundtrip_all_fields_ids_and_regions(self):
        self.run_case('roundtrip')

    def test_empty_and_maximum_active_dormant_capacity(self):
        self.run_case('empty_capacity')

    def test_malformed_header_counts_region_states_and_next_id(self):
        self.run_case('malformed_headers')

    def test_malformed_types_health_and_duplicate_ids(self):
        self.run_case('malformed_records')

    def test_invalid_position_angle_timer_boolean_and_flag_ranges(self):
        self.run_case('malformed_ranges')

    def test_valid_range_boundaries_and_unrestricted_signed_fields(self):
        self.run_case('valid_boundaries')

    def test_every_truncated_prefix_and_following_section_boundary(self):
        output = self.run_case('truncation')
        self.assertRegex(output, r'188 truncated prefixes, [1-9]\d* accepted by raw '
                                r'zero-filling codec; all signal transport EOF')


if __name__ == '__main__':
    unittest.main()
