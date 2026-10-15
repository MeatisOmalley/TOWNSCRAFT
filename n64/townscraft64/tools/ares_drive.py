"""Drive the game in ares: scripted keys and mouse, frame waits, screenshots.

Input goes through g_n64_inject (src/n64/input.c), which the game consumes
once per presented frame, so scripted play uses the same keyboard decoder and
mouse path as real input. Scenarios are plain Python using Session.

    python tools/ares_drive.py smoke     # new game, walk, look, screenshots
"""
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ares_gdb  # noqa: E402

ROOT = ares_gdb.ROOT
ROM = ROOT / 'townscraft64.z64'
ELF = ROOT / 'build/rom/townscraft64.elf'
OUT = ROOT / 'build/drive'
INJECT = struct.Struct('>IIIIiiIII')  # magic frame head tail mouse_dx/dy buttons frame_us frame_us_sum
INJECT_MAGIC = 0x54363449

# AT set-1 make codes (src/n64/input.c decodes through DOScraft keyboard.c).
KEYS = dict(esc=0x01, tab=0x0F, q=0x10, w=0x11, e=0x12, enter=0x1C, ctrl=0x1D, a=0x1E, s=0x1F,
            d=0x20, j=0x24, c=0x2E, comma=0x33, dot=0x34, space=0x39, f1=0x3B, f2=0x3C, f3=0x3D,
            f4=0x3E, f9=0x43, n1=0x02, n2=0x03, n3=0x04, n4=0x05)


class Session:
    def __init__(self, rom=ROM, elf=ELF, expansion_pak=True, name='drive'):
        self.sym = ares_gdb.symbols(elf)
        self.inject = self.sym['g_n64_inject']
        self.out = OUT / name
        self.out.mkdir(parents=True, exist_ok=True)
        settings = ROOT / 'build/ares-drive/settings.bml'
        self.process = ares_gdb.launch(rom, settings, expansion_pak)
        self.gdb = ares_gdb.Gdb(ares_gdb.PORT, time.monotonic() + 30)
        deadline = time.monotonic() + 60
        while self.state()[0] != INJECT_MAGIC:
            if time.monotonic() > deadline:
                raise TimeoutError('game did not present a frame')
            time.sleep(0.25)

    def state(self):
        return INJECT.unpack(self.gdb.read(self.inject, INJECT.size))

    def frame(self):
        return self.state()[1]

    def frames(self, n):
        """Wait until n more frames have been presented."""
        target = self.frame() + n
        while self.frame() < target:
            time.sleep(0.02)

    def scancode(self, code):
        head = self.state()[2]
        self.gdb.write(self.inject + INJECT.size + (head & 255), bytes([code]))
        self.gdb.write(self.inject + 8, struct.pack('>I', head + 1))

    def press(self, key):
        self.scancode(KEYS[key])

    def release(self, key):
        self.scancode(KEYS[key] | 0x80)

    def tap(self, key, hold=2):
        self.press(key)
        self.frames(hold)
        self.release(key)
        self.frames(1)

    def hold(self, key, frames):
        self.press(key)
        self.frames(frames)
        self.release(key)

    def mouse(self, dx, dy):
        self.gdb.write(self.inject + 16, struct.pack('>ii', dx, dy))
        self.frames(1)

    def buttons(self, mask):
        self.gdb.write(self.inject + 24, struct.pack('>I', mask))

    def fps(self, frames=60):
        """Average presented frames per second over the next `frames` frames."""
        f0, sum0 = self.state()[1], self.state()[8]
        self.frames(frames)
        st = self.state()
        n, us = st[1] - f0, st[8] - sum0
        return n * 1e6 / us if us else 0.0

    def read_symbol(self, name, fmt):
        size = struct.calcsize('>' + fmt)
        return struct.unpack('>' + fmt, self.gdb.read(self.sym[name], size))

    def screenshot(self, name):
        path = self.out / f'{name}.png'
        subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'tools/capture_window.ps1'), '-ProcessId', str(self.process.pid),
                        '-Out', str(path)], check=True, capture_output=True)
        return path

    def close(self, graceful=False):
        """Stop ares. graceful closes its window so it writes save files."""
        self.gdb.close()
        if graceful:
            subprocess.run(['taskkill', '/PID', str(self.process.pid)], capture_output=True)
            try:
                self.process.wait(timeout=20)
                return
            except subprocess.TimeoutExpired:
                pass
        self.process.kill()
        self.process.wait()

    def wait_world(self, frames=150):
        """After New Game/Load: let generation/streaming settle."""
        self.frames(frames)


def smoke():
    s = Session(name='smoke')
    try:
        s.frames(30)
        s.screenshot('1-menu')
        s.tap('space')                      # New Game
        t0 = time.monotonic()
        while time.monotonic() - t0 < 120:  # loading/generation
            s.frames(30)
            ticks, = s.read_symbol('g_ticks', 'I')
            if s.frame() > 200:
                break
        s.frames(120)
        s.screenshot('2-world')
        s.hold('w', 60)
        s.screenshot('3-walked')
        for _ in range(10):
            s.mouse(40, 0)
        s.frames(10)
        s.screenshot('4-turned')
        print(f'fps looking at terrain: {s.fps(120):.1f}')
        print('frames', s.frame(), 'ticks', s.read_symbol('g_ticks', 'I')[0])
    finally:
        s.close()


def save():
    """Place a torch, save (F9), restart ares, load: the torch must be back."""
    saves = ROOT / 'build/ares-drive/saves'
    for f in saves.glob('*'):
        f.unlink()
    s = Session(name='save')
    try:
        s.frames(30)
        s.tap('space')                      # New Game
        s.wait_world(240)
        for _ in range(8):
            s.mouse(0, 30)                  # look down at the ground
        s.frames(10)
        s.tap('e')                          # place the torch (hotbar slot 1)
        s.frames(20)
        s.screenshot('1-placed')
        s.tap('f9')                         # save
        s.frames(120)
        s.screenshot('2-saved')
    finally:
        s.close(graceful=True)
    print('save files:', [(f.name, f.stat().st_size) for f in saves.glob('*')])
    s = Session(name='save')
    try:
        s.frames(30)
        s.screenshot('3-menu-after-restart')
        s.tap('s')                          # down to Load Game
        s.tap('space')
        s.wait_world(240)
        s.screenshot('4-loaded')
    finally:
        s.close(graceful=True)


if __name__ == '__main__':
    {'smoke': smoke, 'save': save}[sys.argv[1] if len(sys.argv) > 1 else 'smoke']()
