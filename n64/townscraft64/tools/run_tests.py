"""Run the Towns host tests as N64 ROMs in the pinned ares and collect results.

Each ROM (built by `python tools/build.py tests`) links tests/harness.c, which
captures stdout and the exit code into `g_test_result`. This script starts
ares with its GDB server enabled, reads that record over TCP (GDB remote
protocol `m` packets; reading does not pause emulation), then closes ares.

Usage: python tools/run_tests.py [--build] [--timeout SECONDS] [test names...]
Results: build/test-results/results.json plus one log per test.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
ARES = ROOT / 'build/deps/ares/ares-v148/ares.exe'
NM = ROOT / 'build/deps/toolchain/bin/mips64-elf-nm.exe'
TESTS = ('player_test', 'mobs_test', 'mob_regions_test')
TICKS_PER_SECOND = 46_875_000  # libdragon get_ticks(): half the 93.75 MHz CPU clock
MAGIC = 0x54363454
HEADER = struct.Struct('>IiiIIIII')  # matches test_result_t in tests/harness.c
PORT = 9123


class Gdb:
    """Just enough of the GDB remote protocol to read memory."""

    def __init__(self, port, deadline):
        while True:
            try:
                self.sock = socket.create_connection(('127.0.0.1', port), timeout=5)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise TimeoutError('ares GDB server did not accept a connection')
                time.sleep(0.5)
        # ares drops any client whose first byte is not '+' (an ack), to keep
        # browsers' HTTP requests to localhost out (nall/tcptext-server.cpp).
        self.sock.sendall(b'+')
        self.buffer = b''

    def packet(self, payload):
        checksum = sum(payload.encode()) & 0xFF
        self.sock.sendall(f'${payload}#{checksum:02x}'.encode())
        while True:
            start = self.buffer.find(b'$')
            end = self.buffer.find(b'#', start + 1) if start >= 0 else -1
            if start >= 0 and end >= 0 and len(self.buffer) >= end + 3:
                body = self.buffer[start + 1:end].decode()
                self.buffer = self.buffer[end + 3:]
                self.sock.sendall(b'+')
                return body
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError('ares closed the GDB connection')
            self.buffer += chunk

    def read(self, address, length):
        data = b''
        while len(data) < length:
            step = min(2048, length - len(data))
            reply = self.packet(f'm{address + len(data):x},{step:x}')
            if not reply or reply.startswith('E'):
                raise IOError(f'memory read at {address + len(data):#x} failed: {reply!r}')
            data += bytes.fromhex(reply)
        return data

    def close(self):
        self.sock.close()


def symbol_address(elf, name):
    out = subprocess.run([str(NM), str(elf)], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise KeyError(f'{name} not found in {elf}')


def run_one(name, timeout, settings):
    rom = ROOT / f'build/tests/{name}.z64'
    elf = ROOT / f'build/tests/build/rom/{name}.elf'
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
                    record.update(status='passed' if code == 0 else 'failed', exit_code=code,
                                  guest_seconds=round(((t1 - t0) & 0xFFFFFFFF) / TICKS_PER_SECOND, 3),
                                  memory_mb=memory >> 20, log_bytes=log_len,
                                  log_truncated=log_len > capacity,
                                  log=log.decode('utf-8', 'replace'))
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
    parser.add_argument('tests', nargs='*', default=TESTS)
    args = parser.parse_args(argv)
    if args.build:
        subprocess.run([sys.executable, '-I', str(ROOT / 'tools/stage_core.py')], check=True)
        subprocess.run([sys.executable, '-I', str(ROOT / 'tools/build.py'), 'tests'], check=True)
    out = ROOT / 'build/test-results'
    out.mkdir(parents=True, exist_ok=True)
    settings = ROOT / 'build/ares-tests/settings.bml'
    (settings.parent / 'saves').mkdir(parents=True, exist_ok=True)
    results = []
    for name in args.tests:
        record = run_one(name, args.timeout, settings)
        (out / f'{name}.log').write_text(record.get('log', ''), encoding='utf-8')
        results.append({k: v for k, v in record.items() if k != 'log'})
        print(f'{name:22} {record["status"]:8} guest {record.get("guest_seconds", "-")} s, '
              f'wall {record["wall_seconds"]} s', flush=True)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    failed = [r['test'] for r in results if r['status'] != 'passed']
    print(f'{len(results) - len(failed)}/{len(results)} passed' + (f'; not passed: {failed}' if failed else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
