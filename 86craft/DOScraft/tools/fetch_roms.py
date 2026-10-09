"""Fetch the three selected, SHA-256-pinned ROMs into ignored dependencies."""
import hashlib
import json
from pathlib import Path
from urllib.parse import quote
from urllib.request import urlopen
ROOT=Path(__file__).resolve().parents[1]
REV='9596fe8d80acb30b36a8a678c7bbce9a890f8050'
PATHS={
    'machines/asus386/ASUS_ISA-386C_BIOS.bin':
        '83de0f967c7a87bf56b4cf832f9c73fb6c29db2dcdb847d74cc895174fe0dd13',
    'video/et4000/ET4000_V8_06.BIN':
        'db2f8461274922026acc8b2a4fe1b0675d8ce7d6430108b74196527f36ed2c09',
    'hdd/esdi_at/62-000279-061.bin':
        'dc9888282bd5e37262b9607d2c5ba6ca1a7ce5810588a4d1d03f517adc8c0c4d',
}
def main():
    records=[]
    for relative, expected in PATHS.items():
        url=f'https://raw.githubusercontent.com/86Box/roms/{REV}/'+quote(relative)
        target=ROOT/'build/deps/86box/roms'/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():
            with urlopen(url,timeout=60) as response: data=response.read()
            if hashlib.sha256(data).hexdigest()!=expected:
                raise RuntimeError(f'{relative}: downloaded ROM checksum mismatch')
            with target.open('xb') as output: output.write(data)
        digest=hashlib.sha256(target.read_bytes()).hexdigest()
        if digest!=expected:
            raise RuntimeError(f'{relative}: existing ROM checksum mismatch; not overwritten')
        records.append(dict(path=relative,sha256=digest,url=url))
    (ROOT/'build/deps/roms.json').write_text(json.dumps(records,indent=2)+'\n')
    print(json.dumps(records,indent=2))
if __name__=='__main__': main()
