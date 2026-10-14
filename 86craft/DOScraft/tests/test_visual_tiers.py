"""Real plant helper: normal crossed geometry preserved, low-res billboard gate."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported,dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

def cross(data):
    text=data.decode().replace('\r\n','\n')
    start=text.index('static void draw_cross(')
    return text[start:text.index('\nstatic void draw_model(',start)]

class VisualTierTests(unittest.TestCase):
    def test_fill_rows_without_planar_keeps_source_unchanged(self):
        data=(build_imported.VENDOR/'src/raster.c').read_bytes()
        self.assertEqual(dos_sources.adapt('raster.c',data,fill_rows=True),
                         dos_sources.adapt('raster.c',data))

    @unittest.skipUnless(ZIG.is_file(),'native Zig required')
    def test_actual_plant_gate_and_camera_facing_quad(self):
        original=(build_imported.VENDOR/'src/render.c').read_bytes()
        reference,_=dos_sources.adapt('render.c',original)
        candidate,recipe=dos_sources.adapt('render.c',original,plant_billboards=True)
        replay=original
        for step in recipe: replay=replay.replace(step['before'].encode(),step['after'].encode())
        self.assertEqual(candidate,replay)
        program=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef struct { int c[3]; } V3;
typedef struct { int x,y,z,u,v; } CVert;
static V3 TX[4],TY[4],TZ[4],AX16[17],AY16[17],AZ16[17];
static u8 tile[256];
static CVert quads[2][4];
static int calls,dos_very_low_mode;
#define DOS_VERY_LOW
#define TEX_TILE(t,l) (tile)
static int uvc(int k) { return k<<16; }
static void draw_cpoly(CVert *v,int n,const u8 *texture,int flags,int flat) {
    assert(calls<2 && n==4 && texture==tile && flags==1 && flat==0);
    memcpy(quads[calls++],v,sizeof(quads[0]));
}
'''+cross(reference).replace('draw_cross(', 'reference_cross(')+cross(candidate)+r'''
int main(void) {
    for(int scene=0;scene<32;++scene) {
        for(int i=0;i<4;++i) for(int c=0;c<3;++c) {
            TX[i].c[c]=(i+scene)*(c+3); TY[i].c[c]=(i+scene)*(c+5); TZ[i].c[c]=(i+scene)*(c+7);
        }
        for(int i=0;i<=16;++i) for(int c=0;c<3;++c) {
            AX16[i].c[c]=i*(c+scene); AY16[i].c[c]=i*(c+2+scene); AZ16[i].c[c]=i*(c+4+scene);
        }
        CVert expected[2][4];
        dos_very_low_mode=0; calls=0; reference_cross(1,1,1,0,15);
        assert(calls==2); memcpy(expected,quads,sizeof(expected));
        calls=0; draw_cross(1,1,1,0,15);
        assert(calls==2 && !memcmp(expected,quads,sizeof(expected)));
        dos_very_low_mode=1; calls=0; draw_cross(1,1,1,0,15);
        assert(calls==1);
        assert(quads[0][1].x-quads[0][0].x==224);
        assert(quads[0][1].y==quads[0][0].y && quads[0][1].z==quads[0][0].z);
        assert(quads[0][0].y-quads[0][3].y==AY16[16].c[1]);
        assert(quads[0][0].u==0 && quads[0][1].u==16*65536);
        assert(quads[0][0].v==0 && quads[0][3].v==16*65536);
    }
    puts("PASS: crossed plants preserved and low-res single upright camera-facing quad");
}
'''
        with tempfile.TemporaryDirectory(prefix='doscraft-plants-') as name:
            folder=Path(name); source=folder/'plants.c'; source.write_text(program);exe=folder/'plants.exe'
            r=subprocess.run([str(ZIG),'cc','-target','x86-windows-gnu','-O2',str(source),'-o',str(exe)],capture_output=True,text=True,timeout=120)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
            self.assertEqual(r.returncode,0,r.stderr);self.assertIn('PASS:',r.stdout)

if __name__=='__main__': unittest.main()
