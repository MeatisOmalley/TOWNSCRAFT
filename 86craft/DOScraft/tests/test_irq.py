"""Native IRQ mocks and DJGPP object contracts; no guest/hardware proof.

The native runner includes the unchanged irq.c with mocked DPMI/port services.
It never executes irq_entry.S, CLI/STI, IRET, or real hardware port accesses.
Disassembly checks establish emitted instructions, not their runtime safety.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DOS = ROOT / 'src/platform/dos'
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0, str(ROOT / 'tools'))
import build_adapters as adapters
import build_imported as imported


def run(command, timeout=120):
    result = subprocess.run(list(map(str, command)), capture_output=True,
                            text=True, errors='replace', timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result.stdout


def mock_headers(directory):
    # All fixture writes stay in private temporary storage. sys.h/common.h are
    # the real hash-verified staged declarations, including the 32-bit ABI.
    headers = {
        'crt0.h': '#define _CRT0_FLAG_UNIX_SBRK 0x0800\n'
                  '#define _CRT0_FLAG_NONMOVE_SBRK 0\n'
                  '#define _CRT0_FLAG_LOCK_MEMORY 0x1000\n'
                  'extern int _crt0_startup_flags;\n',
        'go32.h': 'unsigned short _my_ds(void);\nunsigned short _my_cs(void);\n',
        'pc.h': 'unsigned char inportb(unsigned short);\n'
                'void outportb(unsigned short, unsigned char);\n',
        'dpmi.h': '''#ifndef TEST_DPMI_H
#define TEST_DPMI_H
typedef struct { unsigned long offset32; unsigned short selector; } __dpmi_paddr;
typedef struct { unsigned long handle, size, address; } __dpmi_meminfo;
typedef struct {
    unsigned char major, minor;
    unsigned short flags;
    unsigned char cpu, master_pic, slave_pic;
} __dpmi_version_ret;
int __dpmi_get_version(__dpmi_version_ret *);
int __dpmi_get_protected_mode_interrupt_vector(int, __dpmi_paddr *);
int __dpmi_set_protected_mode_interrupt_vector(int, __dpmi_paddr *);
int __dpmi_get_segment_base_address(int, unsigned long *);
int __dpmi_lock_linear_region(__dpmi_meminfo *);
int __dpmi_unlock_linear_region(__dpmi_meminfo *);
int __dpmi_get_and_disable_virtual_interrupt_state(void);
int __dpmi_get_and_set_virtual_interrupt_state(int);
#endif
''',
    }
    for name, content in headers.items():
        (directory / name).write_text(content, encoding='utf-8')


@unittest.skipUnless(ZIG.is_file(), 'existing native Zig toolchain unavailable')
class NativeIRQTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='doscraft-irq-native-')
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        imported.stage_sources(imported.VENDOR, directory / 'imported')
        mock_headers(directory)
        cls.executable = directory / 'irq_test.exe'
        run([ZIG, 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2',
             '-Wall', '-Wextra', '-Werror', '-I', directory,
             '-I', directory / 'imported/src', '-I', DOS,
             ROOT / 'tests/irq_test.c', '-o', cls.executable], timeout=180)

    def check_case(self, case, *arguments):
        output = run([self.executable, case, *arguments], timeout=20)
        self.assertIn(f'PASS: {case}', output)

    def test_phase_exhaustive_boundaries_and_million_step_wraps(self):
        self.check_case('phase')

    def test_timer_body_tick_wrap_hook_profiler_and_bios_cadence(self):
        self.check_case('timer')

    def test_keyboard_status_filtering_and_eoi(self):
        self.check_case('keyboard')

    def test_init_shutdown_and_reinit_preserve_interrupt_state(self):
        for state in (0, 1):
            for pending in (0, 3, 40):
                with self.subTest(state=state, pending=pending):
                    self.check_case('lifecycle', state, pending)

    def test_init_failure_unwind_and_retry(self):
        for failure in ('unix_sbrk', 'version', 'get_timer', 'get_keyboard',
                        'segment', 'lock', 'atexit', 'install_timer', 'install_keyboard'):
            for state in (0, 1):
                with self.subTest(failure=failure, state=state):
                    self.check_case('init_failure', failure, state)

    def test_live_vector_restore_failure_aborts_before_unlock(self):
        for failure in ('rollback_timer', 'restore_timer', 'restore_keyboard'):
            with self.subTest(failure=failure):
                self.check_case('abort_safety', failure)


def instructions(block):
    """Ignore relocation records; retain opcode and normalized operands."""
    return [re.sub(r'\s+', ' ', match.group(1).strip())
            for line in block.splitlines()
            if (match := re.match(r'^\s*[0-9a-f]+:\s+([a-z][^\t]*)$', line))]


@unittest.skipUnless(imported.COMPILER.is_file(), 'existing DJGPP unavailable')
class DJGPPIRQTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='doscraft-irq-dos-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        # Exercise the actual adapter build's IRQ inclusion in isolated storage.
        cls.record = adapters.build(cls.directory / 'adapters')
        cls.objects = cls.directory / 'adapters/obj'
        cls.nm = imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc', 'nm'))
        cls.objdump = imported.COMPILER.with_name(
            imported.COMPILER.name.replace('gcc', 'objdump'))

    def test_real_build_coff_symbols_and_link(self):
        self.assertEqual(self.record['status'], 'verified_objects')
        by_source = {Path(entry['source']).name: entry for entry in self.record['objects']}
        for name in ('irq.c', 'irq_entry.S'):
            self.assertEqual(by_source[name]['format'], 'coff-i386')
            self.assertEqual(by_source[name]['source_sha256'], imported.sha256((DOS / name).read_bytes()))
        c = by_source['irq.c']['symbols']
        self.assertEqual(c.get('__crt0_startup_flags'), 'D')
        for name in ('dos_irq_init', 'dos_irq_shutdown', 'dos_irq_set_tick_hook',
                     'dos_keyboard_irq_save', 'dos_keyboard_irq_restore',
                     'dos_irq_timer_body', 'dos_irq_keyboard_body'):
            self.assertEqual(c.get('_' + name), 'T')
        for name in ('g_ticks', 'g_profSamples', 'g_profCount', 'g_profEnable',
                     'dos_irq_keyboard_count', 'dos_irq_bios_count',
                     'dos_irq_data_selector', 'dos_irq_code_selector', 'dos_irq_old_timer'):
            self.assertIn(c.get('_' + name), ('B', 'C', 'D'))
        assembly = by_source['irq_entry.S']['symbols']
        self.assertEqual({name for name, kind in assembly.items() if kind == 'T'},
                         {'_dos_irq_timer_entry', '_dos_irq_keyboard_entry'})
        self.assertEqual({name for name, kind in assembly.items() if kind == 'U'},
                         {'_dos_irq_data_selector', '_dos_irq_code_selector', '_dos_irq_old_timer',
                          '_dos_irq_timer_body', '_dos_irq_keyboard_body'})
        self.assertEqual(c.get('_dos_irq_timer_entry'), 'U')
        self.assertEqual(c.get('_dos_irq_keyboard_entry'), 'U')
        # Compile with warnings fatal and the actual staged sys.h/common.h.
        run([imported.COMPILER, *imported.CFLAGS, '-Werror',
             '-I', self.directory / 'adapters/src', '-I', DOS,
             '-c', DOS / 'irq.c', '-o', self.directory / 'irq_strict.o'])
        main = self.directory / 'link_probe.c'
        main.write_text('#include "irq.h"\nint main(void) {\n'
                        'dos_irq_set_tick_hook(0);\n'
                        'if (dos_irq_init()) return 1;\n'
                        'dos_irq_shutdown(); return 0; }\n', encoding='utf-8')
        run([imported.COMPILER, *imported.CFLAGS, '-Werror', '-I', DOS, main,
             *(self.objects / name for name in ('irq.o', 'irq_entry.o', 'keyboard.o')),
             '-o', self.directory / 'irq_link_probe.exe'])
        # Deliberately never execute this DOS binary on the host.

    def test_emitted_entry_context_stack_chain_and_transport_frame(self):
        assembly = run([self.objdump, '-dr', '--no-show-raw-insn', self.objects / 'irq_entry.o'])
        blocks = {name: body for name, body in re.findall(
            r'^[0-9a-f]+ <([^>]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)', assembly,
            flags=re.MULTILINE | re.DOTALL)}
        frame = instructions(blocks['_dos_irq_timer_entry'])
        body = instructions(blocks['no_profile_frame'])
        timer = frame + body
        chain = instructions(blocks['chain_timer'])
        keyboard = instructions(blocks['_dos_irq_keyboard_entry'])
        save = ['cli', 'pusha', 'push %ds', 'push %es', 'push %fs', 'push %gs',
                'mov %cs:0x0,%ax', 'mov %eax,%ds', 'mov %eax,%es', 'cld',
                'mov %esp,%edx', 'xor %ecx,%ecx', 'mov %ss,%cx', 'mov %eax,%ss']
        restore = ['pop %edx', 'pop %eax', 'mov %eax,%ss', 'mov %edx,%esp',
                   'pop %gs', 'pop %fs', 'pop %es', 'pop %ds', 'popa']
        for entry, stack_top in ((timer, 0x2000), (keyboard, 0x4000)):
            self.assertEqual(entry[:len(save)], save)
            self.assertEqual(entry[len(save):len(save) + 3],
                             [f'mov ${stack_top:#x},%esp', 'push %ecx', 'push %edx'])
            end = entry.index('iret')
            self.assertEqual(entry[end - len(restore) - 1:end + 1],
                             restore + ['sti', 'iret'])
            self.assertEqual(entry.count('sti'), 1)
        # Pinned normal CWSDPMI r7 has a three-word transport frame before
        # interrupted EIP/CS/EFLAGS. Flags alone cannot distinguish RM callbacks.
        # Check host CS before reading resident host trampoline bytes through FS;
        # only then read candidate game CS/EIP through original SS in ES.
        outer_eip = (8 + 4) * 4
        outer_cs = outer_eip + 4
        outer_flags = outer_eip + 8
        eip_offset = outer_eip + 3 * 4
        cs_offset = eip_offset + 4
        gate = frame[len(save) + 3:]
        branch_ops = [op for op in gate if op.startswith('jne ')]
        self.assertEqual(len(branch_ops), 5)
        self.assertTrue(all('<no_profile_frame>' in op for op in branch_ops))
        # Normalize only branch addresses, which vary as instructions change.
        gate = ['jne no_profile_frame' if op.startswith('jne ') else op for op in gate]
        self.assertEqual(gate, [
            'mov %ecx,%es', 'xor %eax,%eax', 'xor %ebx,%ebx',
            f'cmpl $0x3002,%es:{outer_flags:#x}(%edx)', 'jne no_profile_frame',
            f'mov %es:{outer_cs:#x}(%edx),%cx', 'cmp 0x4,%cx', 'jne no_profile_frame',
            'mov %ecx,%fs', f'mov %es:{outer_eip:#x}(%edx),%esi',
            'cmpb $0x9a,%fs:(%esi)', 'jne no_profile_frame',
            'cmpw $0x0,%fs:0x1(%esi)', 'jne no_profile_frame',
            f'movzwl %es:{cs_offset:#x}(%edx),%eax', 'cmp 0x0,%ax',
            'jne no_profile_frame', f'mov %es:{eip_offset:#x}(%edx),%ebx',
        ])
        self.assertIn('dir32\t_dos_irq_old_timer', blocks['_dos_irq_timer_entry'])
        self.assertIn('dir32\t_dos_irq_code_selector', blocks['_dos_irq_timer_entry'])
        self.assertEqual(body[:4], ['push %eax', 'push %ebx', 'mov %ds,%cx', 'mov %ecx,%es'])
        self.assertIn('_dos_irq_timer_body', blocks['no_profile_frame'])
        self.assertIn('_dos_irq_keyboard_body', blocks['_dos_irq_keyboard_entry'])
        branch = next(index for index, op in enumerate(timer) if '<chain_timer>' in op)
        self.assertIn('<chain_timer>', timer[branch])
        self.assertEqual(timer[branch - 2:branch], ['add $0x8,%esp', 'test %eax,%eax'])
        self.assertEqual(timer[branch + 1:branch + 3], ['mov $0x20,%al', 'out %al,$0x20'])
        self.assertEqual(chain, restore + ['ljmp *%cs:0x0'])
        self.assertIn('_dos_irq_old_timer', blocks['chain_timer'])
        self.assertFalse(any(op.startswith('out ') for op in keyboard))
        symbols = run([self.nm, '-a', self.objects / 'irq_entry.o'])
        addresses = {name: int(address, 16) for address, name in re.findall(
            r'^([0-9a-f]+) b (\w+)$', symbols, flags=re.MULTILINE)}
        for name in ('timer_stack', 'keyboard_stack'):
            self.assertEqual(addresses[name] % 16, 0)
            self.assertEqual(addresses[name + '_end'] - addresses[name], 8192)
        self.assertLessEqual(addresses['timer_stack_end'], addresses['keyboard_stack'])

    def test_emitted_code_is_integer_only_and_c_guard_has_no_cli_sti(self):
        for name in ('irq.o', 'irq_entry.o'):
            code = run([self.objdump, '-d', '--no-show-raw-insn', self.objects / name])
            ops = instructions(code)
            self.assertTrue(ops)
            # x87 mnemonics start with f; SIMD operands include mm/xmm/ymm/zmm.
            self.assertFalse([op for op in ops if re.match(r'f\w+\b', op)
                              or re.search(r'%(?:[xyz]mm|mm)\d', op)])
            if name == 'irq.o':
                self.assertFalse([op for op in ops if op in ('cli', 'sti', 'iret')])
        symbols = self.record['objects']
        c = next(entry['symbols'] for entry in symbols if Path(entry['source']).name == 'irq.c')
        self.assertEqual(c.get('___dpmi_get_and_disable_virtual_interrupt_state'), 'U')
        self.assertEqual(c.get('___dpmi_get_and_set_virtual_interrupt_state'), 'U')


if __name__ == '__main__':
    unittest.main()
