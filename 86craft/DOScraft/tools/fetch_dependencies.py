"""Download isolated host tools / FreeDOS media, never install system-wide.
Every acquisition checks the URL/hash pairs pinned below and records provenance.
"""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
CTMOUSE_EXE_SHA256='f4cef8579a1699045c37ac0ddf03fe0ca361de5c1003ba375c3e08f01a546d01'
EMULATOR_EXE_SHA256='dc236a27e5fccb07d20a60f471e5c1252fbcd82d0505542dd8e4a15c1689e885'
ASSETS={
    'djgpp':('https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-mingw-gcc1220-standalone.zip','6f88b531d216f4d92668c960b5cde9a829b5611e06d2c3e431041e33f01c1a52'),
    '86box':('https://github.com/86Box/86Box/releases/download/v6.0/86Box-Windows-64-b9001.zip','b86722ed547089f095e8875805411f83c76aafd7d318f67286a5a69aecdd7644'),
    'freedos':('https://download.freedos.org/1.4/FD14-FloppyEdition.zip','45b1fa7c52dd996c3bfa5e352ffcd410781b952a6ad629f15a4c9ec4bbaefc5a'),
    'cwsdpmi':('https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip','deacda0488e1cdd7c4a9f32fab45662b34c0ed6b2d7d4d13bc07041b62004a8c'),
    # 1.9.1 /R11 has linear motion counters. 2.1 beta 4 accelerates even
    # function 0B counters, so it cannot preserve the game's raw camera input.
    'ctmouse191':('https://cutemouse.sourceforge.net/download/cutemouse191.zip','8cf7379cb7d7f01f3031e589286e920f0e8d9d4f73782f30f9f6af5b21329c66'),
}

def acquire(spec):
    name,(url,expected)=spec
    dest=ROOT/'build/deps'/name; dest.mkdir(parents=True,exist_ok=True)
    archive=dest/'download.zip'
    if not archive.exists():
        request=urllib.request.Request(url,headers={'User-Agent':'DOScraft-development'})
        with urllib.request.urlopen(request,timeout=90) as response, archive.open('xb') as output:
            while data:=response.read(1024*1024): output.write(data)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    if expected and expected!=digest: raise RuntimeError(f'{name} checksum mismatch; not extracted')
    with zipfile.ZipFile(archive) as zipped:
        for info in zipped.infolist():
            target=(dest/info.filename).resolve()
            if not target.is_relative_to(dest.resolve()): raise RuntimeError('Unsafe archive path')
        zipped.extractall(dest)
    print(f'{name}: {digest}',flush=True)
    return dict(name=name,url=url,sha256=digest,bytes=archive.stat().st_size)

def main():
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results=list(pool.map(acquire,ASSETS.items()))
    (ROOT/'build/deps/acquired.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__': main()
