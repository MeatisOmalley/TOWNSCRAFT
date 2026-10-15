"""Talk to the pinned ares through its GDB server: launch, read/write RDRAM.

Shared by run_tests.py (test results) and ares_drive.py (scripted play).
ares drops any client whose first byte is not '+' (nall/tcptext-server.cpp),
and memory reads/writes do not pause emulation. The server is bound to IPv6
loopback only, so nothing listens on the network.
"""
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
ARES = ROOT / 'build/deps/ares/ares-v148/ares.exe'
NM = ROOT / 'build/deps/toolchain/bin/mips64-elf-nm.exe'
PORT = 9123


class Gdb:
    """Just enough of the GDB remote protocol for memory access."""

    def __init__(self, port, deadline):
        while True:
            try:
                self.sock = socket.create_connection(('::1', port), timeout=5)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise TimeoutError('ares GDB server did not accept a connection')
                time.sleep(0.5)
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

    def write(self, address, data):
        for at in range(0, len(data), 1024):
            chunk = data[at:at + 1024]
            reply = self.packet(f'M{address + at:x},{len(chunk):x}:{chunk.hex()}')
            if reply != 'OK':
                raise IOError(f'memory write at {address + at:#x} failed: {reply!r}')

    def close(self):
        self.sock.close()


def symbols(elf):
    """Map of symbol name to address from mips64-elf-nm."""
    out = subprocess.run([str(NM), str(elf)], check=True, capture_output=True, text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return table


ARES_RANDNET = ROOT / 'build/deps/ares-randnet/ares.exe'


def launch(rom, settings, expansion_pak=True, port=PORT, devices=None):
    """Start ares on a ROM with its GDB server enabled and a private settings file."""
    saves = settings.parent / 'saves'
    saves.mkdir(parents=True, exist_ok=True)
    # devices: {port: 'Randnet Keyboard' | 'Mouse' | 'Gamepad'} needs the ares fork.
    exe = ARES_RANDNET if devices else ARES
    args = [str(exe), '--system', 'Nintendo 64', '--no-file-prompt', '--settings-file', str(settings),
            '--setting', f'Nintendo64/ExpansionPak={"true" if expansion_pak else "false"}',
            '--setting', 'Input/Defocus=Allow',
            '--setting', 'DebugServer/Enabled=true',
            # IPv6 mode binds [::1] only (IPv4 mode listens on every interface).
            '--setting', 'DebugServer/UseIPv4=false',
            '--setting', f'DebugServer/Port={port}',
            '--setting', f'Paths/Saves={saves.as_posix()}/']
    for p in range(1, 5) if devices else ():
        args += ['--setting', f'Nintendo64/ControllerPort{p}={devices.get(p, "Gamepad")}']
    args.append(str(rom))
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
