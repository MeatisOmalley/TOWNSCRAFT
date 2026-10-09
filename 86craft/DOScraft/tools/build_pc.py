"""Build a DOS platform probe; 486 primary, legacy 386 diagnostic optional."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from fat_image import FatImage, populate, scratch_hdd

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    '486dx25': dict(role='primary', arch='i486', minimum_cpu=4, fpu_model='internal'),
    '386dx33': dict(role='deferred_compatibility', arch='i386', minimum_cpu=3, fpu_model='387'),
}


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=TARGETS, default='486dx25',
                        help='default: 486 primary; 386 is only a retained diagnostic scaffold')
    parser.add_argument('--prepare-vm', action='store_true')
    parser.add_argument('--probe', choices=('platform', 'display', 'adapter'), default='platform',
                        help='standalone platform, 320x240 display, or game-facing adapter gate')
    parser.add_argument('--hold-display', action='store_true',
                        help='display probe only: hold the test pattern until Escape for visual inspection')
    return parser


def compiler_flags(target):
    spec = TARGETS[target]
    return [f'-march={spec["arch"]}', f'-mtune={spec["arch"]}', '-m80387', '-mfpmath=387',
            f'-DDOSCRAFT_MINIMUM_CPU={spec["minimum_cpu"]}', '-O2', '-Wall', '-Wextra', '-std=gnu99']


def guest_startup(probe, hold_display=False):
    if hold_display and probe != 'display':
        raise ValueError('--hold-display requires --probe display')
    text = (ROOT / 'guest/AUTOEXEC.BAT').read_text().replace('PLATFORM', probe.upper())
    text = text.replace('isolated platform test', f'isolated {probe} test')
    text = text.replace('Platform test finished', f'{probe.capitalize()} test finished')
    if hold_display:
        text = text.replace('DISPLAY.EXE /AUTO', 'DISPLAY.EXE')
    return text.replace('\n', '\r\n').encode('ascii')


def main():
    parser = argument_parser()
    args = parser.parse_args()
    if args.hold_display and args.probe != 'display':
        parser.error('--hold-display requires --probe display')
    target = TARGETS[args.target]
    deps = ROOT / 'build/deps'
    compiler = deps / 'djgpp/djgpp/bin/i586-pc-msdosdjgpp-gcc.exe'
    out = ROOT / 'build/pc' / args.target
    out.mkdir(parents=True, exist_ok=True)
    probe_name = args.probe.upper()
    exe = out / f'{probe_name}.EXE'
    adapter_record = None
    include_flags = []
    if args.probe == 'platform':
        sources = [ROOT / 'src/platform/dos/probe.c']
    elif args.probe == 'display':
        sources = [ROOT / 'src/platform/dos/display_probe.c', ROOT / 'src/platform/dos/video.c',
                   ROOT / 'src/platform/dos/vga_pack.c']
    else:
        if args.target != '486dx25':
            parser.error('Game-facing adapters are only a primary 486 diagnostic')
        from build_adapters import stage
        adapter_dir = out / 'adapter-probe'
        adapter_record = stage(adapter_dir)
        include_flags = ['-I', str(adapter_dir / 'src')]
        sources = [*(ROOT / 'src/platform/dos' / name for name in
                     ('adapter_probe.c', 'gfx.c', 'heap.c', 'video.c', 'vga_pack.c')),
                   adapter_dir / 'src/font.c']
    command = [str(compiler), *compiler_flags(args.target), *include_flags,
               *(str(source) for source in sources), '-o', str(exe)]
    subprocess.run(command, check=True)
    profile = ROOT / 'profiles' / f'{args.target}-platform.cfg'
    manifest = {'stage': f'M1 {args.probe} probe; not the game', 'probe': args.probe,
                'hold_display': args.hold_display,
                'target': args.target,
                **target, 'profile_sha256': hashlib.sha256(profile.read_bytes()).hexdigest(),
                'compiler_command': command,
                'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'source_sha256': {str(source.relative_to(ROOT)): hashlib.sha256(source.read_bytes()).hexdigest()
                                  for source in sources},
                'disk_timing_note': '86Box v6 diagnostic uses generic 1989_3500rpm; not final Wren V timing.'}
    if adapter_record is not None:
        manifest['imported_drawing'] = adapter_record['gfx_selection']
        manifest['imported_font_sha256'] = hashlib.sha256(sources[-1].read_bytes()).hexdigest()
    if args.target == '386dx33':
        manifest['fpu_note'] = 'Actual emulator model is Intel 387, not Cyrix FasMath. 386 game port is deferred.'
    if args.prepare_vm:
        runtime = ROOT / 'runtime'
        runtime.mkdir(exist_ok=True)
        # A new private directory for every run: never overwrite an old HDD.
        vm = Path(tempfile.mkdtemp(prefix=f'{args.probe}-{args.target}-', dir=runtime))
        source = FatImage((deps / 'freedos/144m/x86BOOT.img').read_bytes())
        files = [('KERNEL.SYS', source.read('KERNEL.SYS')),
                 ('COMMAND.COM', source.read('FREEDOS/BIN/COMMAND.COM')),
                 ('CWSDPMI.EXE', (deps / 'cwsdpmi/bin/CWSDPMI.EXE').read_bytes()),
                 (f'{probe_name}.EXE', exe.read_bytes())]
        for name in ('FDCONFIG.SYS', 'AUTOEXEC.BAT'):
            if name == 'AUTOEXEC.BAT':
                text = guest_startup(args.probe, args.hold_display)
            else:
                text = (ROOT / 'guest' / name).read_text().replace('\n', '\r\n').encode('ascii')
            files.append((name, text))
        floppy = populate(source.data[:512], files)
        with (vm / 'boot.img').open('xb') as f:
            f.write(floppy)
        with (vm / 'scratch.img').open('xb') as f:
            f.write(scratch_hdd())
        config = profile.read_text()
        (vm / '86box.cfg').write_text(config, encoding='utf-8')
        manifest['vm_directory'] = str(vm)
        manifest['disk_note'] = 'New 20 MiB scratch FAT16 image; final large-world HDD not yet configured.'
        (vm / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (out / 'latest-vm.txt').write_text(str(vm))
        print(f'VM={vm}')
    (out / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {exe}')


if __name__ == '__main__':
    main()
