"""Fetch SHA-256-pinned primary/legacy diagnostic ROMs into ignored dependencies."""
import hashlib
import json
from pathlib import Path
from urllib.parse import quote
from urllib.request import urlopen
ROOT=Path(__file__).resolve().parents[1]
REV='9596fe8d80acb30b36a8a678c7bbce9a890f8050'
PATHS={
    'machines/isa486/ISA-486.BIN':
        '3b6e985cb94a6d37963f481a07cd6b1c84a698d6ba41fe4166298532b7c9a2be',
    'machines/asus386/ASUS_ISA-386C_BIOS.bin':
        '83de0f967c7a87bf56b4cf832f9c73fb6c29db2dcdb847d74cc895174fe0dd13',
    'video/et4000/ET4000_V8_06.BIN':
        'db2f8461274922026acc8b2a4fe1b0675d8ce7d6430108b74196527f36ed2c09',
    'hdd/esdi_at/62-000279-061.bin':
        'dc9888282bd5e37262b9607d2c5ba6ca1a7ce5810588a4d1d03f517adc8c0c4d',
    'machines/av4/amibios_486dx_isa_bios_aa4025963.bin':
        '4f524abc4a9011ce5ff8e3decbfd5cac2eb8e5ff894a00dfa67b73fe507676e8',
    'scsi/adaptec/aha1542c102.bin':
        '7d5bbfbb5af6d75e5aa6546601f715df586f1a262bfc8e1eb82313bd546d821c',
    'scsi/adaptec/aha1540a307.bin':
        'a8a5d2ebd6c25f8073d279b23c5bda554deceb5b16991545354ad663327dc67d',
    'video/et4000w32/ET4000W32VLB_bios_MX27C512.BIN':
        '4d2dafc08b78e9d216408d6c7fa53dca404fb8dfa10d6d7fddabac8e767b050d',
    # 1996 Pentium VM (HARDWARE_1996.md).
    'machines/p55t2p4/0207_j2.bin':
        '3cdabde2f4abbed74887ec714c2c87d565dc76a0fc8ee7b681c9bd82add20ecd',
    'video/matrox/matrox2064wr2.BIN':
        '68aeb40cce41d6a9a7b4d4198dd74e0f126b2c3646b4053a44a561f81df104b3',
    'sound/creative/CT4540 PnP.BIN':
        '6011f34d1c9d8ede8425df0a945acf31dcb6d57302085e471489000b3d9b37de',
    'sound/creative/awe32.raw':
        '4e143b94f758734f594ded78f4e5115635975c16fa37fabeae0baa4938ca710e',
    'scsi/ncr53c8xx/NCR307.BIN':
        '87aa743acf8e416bf53e6266dc3ef119e515fa8da5071045577a215440b09365',
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
