"""Build the first 386 DOS executable and an isolated bootable test machine."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from fat_image import FatImage, populate, scratch_hdd

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-vm', action='store_true')
    args = parser.parse_args()
    deps = ROOT / 'build/deps'
    compiler = deps / 'djgpp/djgpp/bin/i586-pc-msdosdjgpp-gcc.exe'
    out = ROOT / 'build/pc'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'PLATFORM.EXE'
    command = [str(compiler), '-march=i386', '-mtune=i386', '-O2', '-Wall', '-Wextra',
               '-std=gnu99', str(ROOT / 'src/platform/dos/probe.c'), '-o', str(exe)]
    subprocess.run(command, check=True)
    manifest = {'stage': 'M1 platform probe; not the game', 'compiler_command': command,
                'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
    if args.prepare_vm:
        runtime = ROOT / 'runtime'
        runtime.mkdir(exist_ok=True)
        # A new private directory for every run: never overwrite an old HDD.
        vm = Path(tempfile.mkdtemp(prefix='platform-', dir=runtime))
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
        config = (ROOT / 'profiles/386dx33-platform.cfg').read_text()
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
