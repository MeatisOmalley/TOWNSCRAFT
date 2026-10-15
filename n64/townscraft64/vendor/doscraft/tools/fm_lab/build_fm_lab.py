"""Build the FM instrument lab: Nuked OPL3 -> WebAssembly, then one HTML page.

    uv run --no-project --with ziglang python tools/fm_lab/build_fm_lab.py

Steps:
  1. Compile nuked/opl3.c (unmodified upstream) and src/opl_wasm.c to
     opl3.wasm with zig's clang (`python -m ziglang cc`, from the ziglang
     package on PyPI, or a `zig` on PATH).
  2. Translate opl3.wasm to JavaScript with Binaryen's wasm2js (run through
     npx, pinned below), writing opl3_asm.js. The page uses it only where a
     security policy refuses WebAssembly; its output is bit-identical.
     --skip-wasm skips steps 1 and 2 and reuses the committed files.
  3. Inline the WebAssembly (base64), the wasm2js engine, the AudioWorklet
     source and the page script into src/lab.html, writing fm_lab.html.
     --artifact PATH also writes the page body alone, the form a claude.ai
     Artifact publish expects.
"""
import argparse
import base64
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
SRC = HERE / 'src'
WASM = HERE / 'opl3.wasm'
ASM = HERE / 'opl3_asm.js'
PAGE = HERE / 'fm_lab.html'
SOURCES = [SRC / 'opl_wasm.c', HERE / 'nuked/opl3.c']
CFLAGS = ['--target=wasm32-wasi', '-O2', '-mexec-model=reactor', '-Wl,--strip-all']
BINARYEN = 'binaryen@123.0.0'
WASM2JS_FLAGS = ['--enable-bulk-memory', '--enable-bulk-memory-opt', '--enable-sign-ext',
                 '--enable-mutable-globals', '--enable-nontrapping-float-to-int', '-O2']
HEAD = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
        '</head><body style="margin:0">\n')
TAIL = '\n</body></html>\n'


def zig_command():
    if importlib.util.find_spec('ziglang'):
        return [sys.executable, '-m', 'ziglang']
    zig = shutil.which('zig')
    if zig:
        return [zig]
    sys.exit('zig not found: run with `uv run --no-project --with ziglang python ...` or install zig')


def build_wasm():
    cmd = zig_command() + ['cc', *CFLAGS, '-o', str(WASM), *map(str, SOURCES)]
    subprocess.run(cmd, check=True)
    print(f'{WASM.name}: {WASM.stat().st_size} bytes')


def build_asm():
    npx = shutil.which('npx')
    if not npx:
        sys.exit('npx not found: install Node.js to run Binaryen wasm2js')
    cmd = [npx, '--yes', '-p', BINARYEN, 'wasm2js', str(WASM), *WASM2JS_FLAGS, '-o', str(ASM)]
    subprocess.run(cmd, check=True)
    print(f'{ASM.name}: {ASM.stat().st_size} bytes')


def asm_expression():
    """opl3_asm.js (an ES module) as one expression yielding its asmFunc."""
    text = ASM.read_text(encoding='utf-8')
    cut = re.search(r'^var retasmFunc = asmFunc\(', text, re.M)
    if not cut:
        sys.exit(f'{ASM.name}: unexpected wasm2js output (no retasmFunc)')
    return '(function () {\n' + text[:cut.start()] + 'return asmFunc;\n})()'


def build_page():
    wasm = base64.b64encode(WASM.read_bytes()).decode('ascii')
    asm = asm_expression()
    engine = (SRC / 'engine.js').read_text(encoding='utf-8')
    worklet = (SRC / 'worklet.js').read_text(encoding='utf-8')
    script = (SRC / 'lab.js').read_text(encoding='utf-8')
    for name, text in (('opl3_asm.js', asm), ('engine.js', engine), ('worklet.js', worklet), ('lab.js', script)):
        if '</script' in text.lower():
            sys.exit(f'{name} contains "</script", which would end the inline script')
    worklet_source = f'const oplAsmFunc = {asm};\n{engine}\n{worklet}'
    body = (SRC / 'lab.html').read_text(encoding='utf-8')
    for key, value in (('WASM_BASE64', wasm), ('WORKLET_SOURCE', json.dumps(worklet_source)),
                       ('OPL_ASM', asm), ('ENGINE_JS', engine.rstrip()), ('LAB_JS', script.rstrip())):
        body = body.replace(f'@@{key}@@', value)
    if '@@' in body:
        sys.exit('lab.html still has an unreplaced @@ placeholder')
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--skip-wasm', action='store_true', help='Reuse the existing opl3.wasm and opl3_asm.js')
    parser.add_argument('--artifact', type=Path, help='Also write the page body for an Artifact publish')
    args = parser.parse_args()
    if not args.skip_wasm:
        build_wasm()
        build_asm()
    body = build_page()
    PAGE.write_text(HEAD + body + TAIL, encoding='utf-8', newline='\n')
    print(f'{PAGE.name}: {PAGE.stat().st_size} bytes')
    if args.artifact:
        args.artifact.write_text(body + '\n', encoding='utf-8', newline='\n')
        print(f'{args.artifact}: {args.artifact.stat().st_size} bytes')


if __name__ == '__main__':
    main()
