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
    return parser


def compiler_flags(target):
    spec = TARGETS[target]
    return [f'-march={spec["arch"]}', f'-mtune={spec["arch"]}', '-m80387', '-mfpmath=387',
            f'-DDOSCRAFT_MINIMUM_CPU={spec["minimum_cpu"]}', '-O2', '-Wall', '-Wextra', '-std=gnu99']


def main():
    args = argument_parser().parse_args()
    target = TARGETS[args.target]
    deps = ROOT / 'build/deps'
    compiler = deps / 'djgpp/djgpp/bin/i586-pc-msdosdjgpp-gcc.exe'
    out = ROOT / 'build/pc' / args.target
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'PLATFORM.EXE'
    command = [str(compiler), *compiler_flags(args.target),
               str(ROOT / 'src/platform/dos/probe.c'), '-o', str(exe)]
    subprocess.run(command, check=True)
    profile = ROOT / 'profiles' / f'{args.target}-platform.cfg'
    manifest = {'stage': 'M1 platform probe; not the game', 'target': args.target,
                **target, 'profile_sha256': hashlib.sha256(profile.read_bytes()).hexdigest(),
                'compiler_command': command,
                'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
    if args.target == '386dx33':
        manifest['fpu_note'] = 'Actual emulator model is Intel 387, not Cyrix FasMath. 386 game port is deferred.'
    if args.prepare_vm:
        runtime = ROOT / 'runtime'
        runtime.mkdir(exist_ok=True)
        # A new private directory for every run: never overwrite an old HDD.
        vm = Path(tempfile.mkdtemp(prefix=f'platform-{args.target}-', dir=runtime))
        source = FatImage((deps / 'freedos/144m/x86BOOT.img').read_bytes())
        files = [('KERNEL.SYS', source.read('KERNEL.SYS')),
                 ('COMMAND.COM', source.read('FREEDOS/BIN/COMMAND.COM')),
                 ('CWSDPMI.EXE', (deps / 'cwsdpmi/bin/CWSDPMI.EXE').read_bytes()),
                 ('PLATFORM.EXE', exe.read_bytes())]
        for name in ('FDCONFIG.SYS', 'AUTOEXEC.BAT'):
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
