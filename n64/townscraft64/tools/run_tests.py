"""Run the Towns host tests as N64 ROMs in the pinned ares and collect results.

Each ROM (built by `python tools/build.py tests`) links tests/harness.c, which
captures stdout and the exit code into `g_test_result`. This script starts
ares with its GDB server enabled, reads that record over TCP (GDB remote
protocol `m` packets; reading does not pause emulation), then closes ares.

Usage: python tools/run_tests.py [--build] [--timeout SECONDS] [test names...]
Runs the Towns tests in build/tests and the DOScraft tests in build/dtests.
Results: build/test-results/results.json plus one log per test.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from ares_gdb import ARES, NM, PORT, Gdb  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
# Towns host tests (build/tests) expect exit 0 and a PASS line. DOScraft's
# tests (build/dtests) carry their expected outcome in build/dtests/expect.json.
TOWNS_TESTS = ('player_test', 'mobs_test', 'mob_regions_test')


def targets():
    found = {name: dict(dir='build/tests', exit_zero=True, contains='PASS') for name in TOWNS_TESTS}
    expect = ROOT / 'build/dtests/expect.json'
    if expect.is_file():
        for name, rule in json.loads(expect.read_text(encoding='utf-8')).items():
            found[name] = dict(dir='build/dtests', **rule)
    return found
TICKS_PER_SECOND = 46_875_000  # libdragon get_ticks(): half the 93.75 MHz CPU clock
MAGIC = 0x54363454
HEADER = struct.Struct('>IiiIIIII')  # matches test_result_t in tests/harness.c


def symbol_address(elf, name):
    out = subprocess.run([str(NM), str(elf)], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise KeyError(f'{name} not found in {elf}')


def run_one(name, target, timeout, settings):
    rom = ROOT / target['dir'] / f'{name}.z64'
    elf = ROOT / target['dir'] / f'build/rom/{name}.elf'
    address = symbol_address(elf, 'g_test_result')
    args = [str(ARES), '--system', 'Nintendo 64', '--no-file-prompt', '--settings-file', str(settings),
            '--setting', 'Nintendo64/ExpansionPak=true',
            '--setting', 'Input/Defocus=Allow',
            '--setting', 'DebugServer/Enabled=true',
            '--setting', 'DebugServer/UseIPv4=true',
            '--setting', f'DebugServer/Port={PORT}',
            '--setting', f'Paths/Saves={(settings.parent / "saves").as_posix()}/',
            str(rom)]
    started = time.monotonic()
    process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    record = dict(test=name, rom=str(rom.relative_to(ROOT)), status='timeout')
    try:
        gdb = Gdb(PORT, started + 30)
        try:
            # The harness record may be in a 0x80... (KSEG0) or sign-extended form.
            for candidate in (address, address | 0xFFFFFFFF00000000):
                try:
                    gdb.read(candidate, 4)
                    address = candidate
                    break
                except IOError:
                    continue
            while time.monotonic() - started < timeout:
                magic, state, code, log_len, t0, t1, memory, capacity = HEADER.unpack(
                    gdb.read(address, HEADER.size))
                if magic == MAGIC and state == 2:
                    log = gdb.read(address + HEADER.size, min(log_len, capacity))
                    text = log.decode('utf-8', 'replace')
                    ok = (code == 0) == target['exit_zero'] and target['contains'] in text
                    record.update(status='passed' if ok else 'failed', exit_code=code,
                                  expected=('exit 0' if target['exit_zero'] else 'nonzero exit') +
                                  f', output contains {target["contains"]!r}',
                                  guest_seconds=round(((t1 - t0) & 0xFFFFFFFF) / TICKS_PER_SECOND, 3),
                                  memory_mb=memory >> 20, log_bytes=log_len,
                                  log_truncated=log_len > capacity,
                                  log=text)
                    break
                time.sleep(1.0)
            else:
                # Timed out: keep what the test printed, to show where it stopped.
                if magic == MAGIC:
                    log = gdb.read(address + HEADER.size, min(log_len, capacity))
                    record.update(log_bytes=log_len, log=log.decode('utf-8', 'replace'))
        finally:
            gdb.close()
    finally:
        record['wall_seconds'] = round(time.monotonic() - started, 1)
        process.kill()
        process.wait()
    return record


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--build', action='store_true', help='stage the core and build test ROMs first')
    # The current tests finish in seconds; a long wait means a hang or crash.
    parser.add_argument('--timeout', type=float, default=120, help='per-test limit in seconds')
    parser.add_argument('tests', nargs='*', help='default: every built test')
    args = parser.parse_args(argv)
    if args.build:
        for tool in ('stage_core.py', 'stage_doscraft_tests.py'):
            subprocess.run([sys.executable, '-I', str(ROOT / 'tools' / tool)], check=True)
        subprocess.run([sys.executable, '-I', str(ROOT / 'tools/build.py'), 'tests', 'dtests'], check=True)
    known = targets()
    unknown = [n for n in args.tests if n not in known]
    if unknown:
        parser.error(f'unknown tests {unknown}; known: {sorted(known)}')
    out = ROOT / 'build/test-results'
    out.mkdir(parents=True, exist_ok=True)
    settings = ROOT / 'build/ares-tests/settings.bml'
    (settings.parent / 'saves').mkdir(parents=True, exist_ok=True)
    results = []
    for name in args.tests or list(known):
        record = run_one(name, known[name], known[name].get('timeout', args.timeout), settings)
        (out / f'{name}.log').write_text(record.get('log', ''), encoding='utf-8')
        results.append({k: v for k, v in record.items() if k != 'log'})
        print(f'{name:34} {record["status"]:8} guest {record.get("guest_seconds", "-")} s, '
              f'wall {record["wall_seconds"]} s', flush=True)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    failed = [r['test'] for r in results if r['status'] != 'passed']
    print(f'{len(results) - len(failed)}/{len(results)} passed' + (f'; not passed: {failed}' if failed else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
