"""Native keyboard parity and DJGPP linkage; no IRQ installation/guest claim."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported

# Physical AT set-1 positions, independent of the adapter's table. Logical
# values come ONLY from the hash-verified pinned sys.h, never rewritten controls.
BASE = {
    0x01: 'ESC', 0x02: '1', 0x03: '2', 0x04: '3', 0x05: '4',
    0x06: '5', 0x07: '6', 0x08: '7', 0x09: '8', 0x0A: '9',
    0x0B: '0', 0x0C: 'MINUS', 0x0E: 'BACKSPACE', 0x0F: 'TAB',
    0x10: 'Q', 0x11: 'W', 0x12: 'E', 0x13: 'R', 0x14: 'T',
    0x15: 'Y', 0x16: 'U', 0x17: 'I', 0x18: 'O', 0x19: 'P',
    0x1C: 'RETURN', 0x1D: 'CTRL', 0x1E: 'A', 0x1F: 'S',
    0x20: 'D', 0x21: 'F', 0x22: 'G', 0x23: 'H', 0x24: 'J',
    0x25: 'K', 0x26: 'L', 0x2A: 'SHIFT', 0x2C: 'Z', 0x2D: 'X',
    0x2E: 'C', 0x2F: 'V', 0x30: 'B', 0x31: 'N', 0x32: 'M',
    0x33: 'COMMA', 0x34: 'DOT', 0x35: 'SLASH', 0x36: 'SHIFT',
    0x39: 'SPACE', 0x48: 'NUM_8', 0x4B: 'NUM_4', 0x4C: 'NUM_5',
    0x4D: 'NUM_6', 0x50: 'NUM_2',
    **{0x3A + number: f'PF{number}' for number in range(1, 11)},
}
EXTENDED = {
    0x1C: 'NUM_RETURN', 0x1D: 'CTRL', 0x47: 'HOME', 0x48: 'UP',
    0x4B: 'LEFT', 0x4D: 'RIGHT', 0x50: 'DOWN', 0x52: 'INSERT', 0x53: 'DELETE',
}
PIN = 'd706a67db6242627c6233e5593fd1a1e1ddc69af'


def verified_sources():
    manifest = json.loads((imported.VENDOR / 'MANIFEST.json').read_bytes())
    if manifest['source_commit'] != PIN or imported.PINNED_COMMIT != PIN:
        raise AssertionError('Pinned Towns revision mismatch')
    entries = {entry['path']: entry for entry in manifest['files']}
    sources = {}
    # Verify every C source before scanning it for actual game key usage.
    paths = ['src/sys.h', *sorted(path for path in entries if path.startswith('src/')
                               and path.endswith('.c'))]
    for path in paths:
        data = (imported.VENDOR / path).read_bytes()
        entry = entries[path]
        if hashlib.sha256(data).hexdigest() != entry['sha256'] or len(data) != entry['size']:
            raise AssertionError(f'Pinned Towns provenance mismatch: {path}')
        blob = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
        if blob != entry['git_blob']:
            raise AssertionError(f'Pinned Towns blob mismatch: {path}')
        sources[path] = data.decode('utf-8').replace('\r\n', '\n')
    return sources


def fixtures(directory, sources):
    """Extract actual Towns queue bodies; rename APIs, replace only IRQ hooks."""
    source = sources['src/sys.c']
    declarations = source.split('#define KEYQ_LEN ', 1)[1].split('\nstruct IdtEntry', 1)[0]
    producer = source.split('static void kbd_byte(u8 d)\n', 1)[1].split('\nstatic void kbd_poll', 1)[0]
    consumers = source.split('int key_get_event(void)\n', 1)[1].split('\nu32 pad_read', 1)[0]
    rename = ('#define g_keyDown reference_keyDown\n'
              '#define key_get_event reference_get_event\n'
              '#define key_flush reference_flush\n')
    reference = directory / 'keyboard_reference.c'
    reference.write_text(rename + '#include "sys.h"\n'
                         'volatile u8 reference_keyDown[128];\n'
                         'static void cli(void) {}\nstatic void sti(void) {}\n'
                         '#define KEYQ_LEN ' + declarations + '\n'
                         'static void kbd_byte(u8 d)\n' + producer + '\n'
                         'int key_get_event(void)\n' + consumers + '\n'
                         'void reference_reset(void) { unsigned int i;\n'
                         'for(i=0;i<128;++i) reference_keyDown[i]=0;\n'
                         'keyQHead=keyQTail=0; kbdFirstByte=0; }\n'
                         'void reference_feed(int key,int release) {\n'
                         'kbd_byte(release ? 0xD0 : 0xC0); kbd_byte((u8)key); }\n'
                         'void reference_repeat(int key) {\n'
                         'kbd_byte(0xF0); kbd_byte((u8)key); }\n', encoding='utf-8')
    rows = [f'    {{{bank},0x{scan:02X},KEY_{name}}},'
            for bank, table in enumerate((BASE, EXTENDED))
            for scan, name in sorted(table.items())]
    (directory / 'keyboard_cases.h').write_text(
        'static const struct Mapping expected[] = {\n' + '\n'.join(rows) + '\n};\n',
        encoding='utf-8')
    return reference


def run(command, timeout=120):
    result = subprocess.run(list(map(str, command)), capture_output=True,
                            text=True, errors='replace', timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


class KeyboardTests(unittest.TestCase):
    def test_verified_pinned_controls_are_covered(self):
        sources = verified_sources()
        defined = set(re.findall(r'\bKEY_[A-Z0-9_]+\b', sources['src/sys.h']))
        used = set()
        for path, source in sources.items():
            if path.endswith('.c') and path != 'src/sys.c':
                used.update(re.findall(r'\bKEY_[A-Z0-9_]+\b', source))
        mapped = {'KEY_' + name for name in (*BASE.values(), *EXTENDED.values())}
        self.assertTrue(used)
        self.assertLessEqual(used, mapped)
        self.assertEqual(defined - mapped, {'KEY_EXECUTE'})
        self.assertFalse(mapped - defined)
        self.assertLessEqual({f'KEY_PF{number}' for number in range(1, 11)}, mapped)
        # The reference queue really retains the pinned 32-slot/drop-new rule.
        self.assertIn('#define KEYQ_LEN 32', sources['src/sys.c'])
        self.assertIn('if(next!=keyQTail)', sources['src/sys.c'])

    @unittest.skipUnless(ZIG.is_file(), 'existing native Zig toolchain unavailable')
    def test_native_translation_queue_and_irq_contract(self):
        sources = verified_sources()
        with tempfile.TemporaryDirectory(prefix='doscraft-keyboard-native-') as temporary:
            directory = Path(temporary)
            reference = fixtures(directory, sources)
            # Exact pinned sys.h, with a declaration-only native common.h.
            # Do not import Towns hw.h or DJGPP's 32-bit pointer assertions.
            (directory / 'sys.h').write_text(sources['src/sys.h'], encoding='utf-8')
            (directory / 'common.h').write_text(
                'typedef unsigned char u8;\ntypedef unsigned int u32;\n', encoding='utf-8')
            executable = directory / 'keyboard_test.exe'
            run([ZIG, 'cc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-I', directory, '-I', ROOT / 'src/platform/dos',
                 ROOT / 'src/platform/dos/keyboard.c', ROOT / 'tests/keyboard_test.c',
                 reference, '-o', executable])
            result = run([executable], timeout=20)
            self.assertIn('PASS: AT mappings, modifiers, sequences, IRQ guards, pinned queue parity',
                          result.stdout)

    @unittest.skipUnless(imported.COMPILER.is_file(), 'existing DJGPP unavailable')
    def test_djgpp_generated_sys_declarations_and_linkage(self):
        sources = verified_sources()
        with tempfile.TemporaryDirectory(prefix='doscraft-keyboard-dos-') as temporary:
            directory = Path(temporary)
            imported.stage_sources(imported.VENDOR, directory / 'imported')
            staged = directory / 'imported/src'
            self.assertEqual((staged / 'sys.h').read_text(), sources['src/sys.h'])
            reference = fixtures(directory, sources)
            objects = []
            for source in (ROOT / 'src/platform/dos/keyboard.c',
                           ROOT / 'tests/keyboard_test.c', reference):
                obj = directory / (source.stem + '.o')
                run([imported.COMPILER, *imported.CFLAGS, '-Werror',
                     '-I', staged, '-I', directory, '-I', ROOT / 'src/platform/dos',
                     '-include', staged / 'sys.h', '-c', source, '-o', obj])
                self.assertEqual(imported.coff_object(obj.read_bytes())['format'], 'coff-i386')
                objects.append(obj)
            run([imported.COMPILER, *objects, '-o', directory / 'keyboard_test.exe'])
            nm = imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc', 'nm'))
            symbols = imported.parse_nm(run([nm, '-g', objects[0]]).stdout)
            for name in ('dos_keyboard_reset', 'dos_keyboard_scancode', 'key_get_event', 'key_flush'):
                self.assertEqual(symbols.get('_' + name), 'T')
            self.assertIn(symbols.get('_g_keyDown'), ('B', 'C'))
            self.assertEqual({name for name, kind in symbols.items() if kind == 'U'},
                             {'_dos_keyboard_irq_save', '_dos_keyboard_irq_restore'})


if __name__ == '__main__':
    unittest.main()
