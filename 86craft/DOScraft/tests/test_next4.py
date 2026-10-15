"""Independent experiment recipes, real flat raster and exact VGA packing."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import shutil

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
BASE=dict(large_world=True,stream_light=True,far_field=True,graphics=True,
    proven_rows=True,raster_finish_skip=True,very_low_texels=True,low_planar=True,
    very_low_planar=True,trap_four=True,flat_inline=True,fill_rows=True,row_pointer=True,
    texel_edx=True,row_mul=True,very_low_local_mesh=True,collect_margin=True,
    collect_swap=True,very_low_collect_margin=True)

class NextTests(unittest.TestCase):
    def run_cmd(self,args):
        r=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=120)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        return r.stdout

    @unittest.skipUnless(ZIG.is_file(),'native Zig required')
    def test_actual_streamer_preserves_critical_requests_and_light_margin(self):
        world,_=dos_sources.adapt('world.c',imported.adapt_source('world.c',
            (imported.VENDOR/'src/world.c').read_bytes())[0],**BASE)
        cache,_=dos_sources.adapt('column_cache.inc',(imported.VENDOR/'src/column_cache.inc').read_bytes(),
            **BASE,vl_stream_radius=True)
        def function(text,signature):
            start=text.index(signature)
            return text[start:text.index('\n}\n',start)+3]
        code=r'''
#include <assert.h>
#include <stdio.h>
#include "world.h"
#define world_column_ready mock_column_ready
#define column_prepare_unchecked column_prepare
int g_NC=64,g_W=1024,g_viewDist=4,dos_potato_mode,g_residentColumns=1;
static short coords[2]; short *g_columnCoords=coords;
static int terrainActive,terrainWriteID=-1,terrainBlocking,cacheLastX=-1,cacheLastZ=-1;
static int cacheDirectionX,cacheDirectionZ,cacheBoot,cacheLightSlot=-1,meshJobCx=-1,meshJobCz=-1,streamReady;
static int gx,gz,loaded,critical,ready,pinned;
static int pin(int x,int z) { return pinned && x==gx && z==gz; }
static int (*columnPin)(int,int)=pin;
static int needed(int *x,int *z) { *x=gx; *z=gz; return critical; }
static int (*columnNeeded)(int *,int *)=needed;
int world_column_ready(int x,int z) { return x!=gx || z!=gz || ready; }
static int terrain_finish_write(void) { return 1; }
static void cache_light_pump(int n) { (void)n; }
static int column_prepare_unchecked(int x,int z) { return 1; }
static int cache_load(int x,int z,int slot) { ++loaded; ready=1; return 1; }
''' + function(world.decode(),'static int column_dist2(') + '\n' + function(cache.decode(),'static void cache_stream(') + r'''
static void reset(void) {
    coords[0]=coords[1]=-1; gx=gz=34; ready=loaded=critical=cacheBoot=pinned=0;
    cacheLastX=cacheLastZ=-1; cacheDirectionX=cacheDirectionZ=0; dos_potato_mode=1;
}
int main(void) {
    reset(); cache_stream(520,520,1); assert(!loaded);
    reset(); critical=1; cache_stream(520,520,1); assert(loaded==1);
    reset(); pinned=1; cache_stream(520,520,1); assert(loaded==1);
    reset(); dos_potato_mode=0; cache_stream(520,520,1); assert(loaded==1);
    reset(); cacheBoot=1; cache_stream(520,520,1); assert(loaded==1);
    reset(); cache_stream(520,520,100000); assert(loaded==1);
    reset(); gx=33; gz=32; cache_stream(520,520,1); assert(loaded==1);
    reset(); gz=32; cache_stream(516,520,1); assert(loaded==1); /* Exactly spawn reach+1. */
    puts("PASS: critical loads, boot, other resolutions and full light-reach margin");
}
'''
        with tempfile.TemporaryDirectory(prefix='dos-next4-stream-') as folder:
            folder=Path(folder); imported.stage_sources(imported.VENDOR,folder/'imported')
            source=folder/'test.c'; source.write_text(code); exe=folder/'test.exe'
            self.run_cmd([ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                '-I',folder/'imported/src',source,'-o',exe])
            self.assertIn('PASS:',self.run_cmd([exe]))

    @unittest.skipUnless(ZIG.is_file() and (ROOT/'build/checks/large-world-proto/terrain1024-lit.bin').is_file(),
        'native Zig and existing lighting fixture required')
    def test_stream_radius_real_lighting_cold_edits_and_reloads(self):
        with tempfile.TemporaryDirectory(prefix='dos-next4-light-') as folder:
            folder=Path(folder); imported.stage_sources(imported.VENDOR,folder)
            src=folder/'src'
            for f in src.iterdir():
                if f.is_file():
                    data,_=dos_sources.adapt(f.name,f.read_bytes(),large_world=True,stream_light=True,vl_stream_radius=True)
                    f.write_bytes(data)
            (folder/'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\nint __dpmi_int(int,__dpmi_regs *);\n')
            tables=src/'tables.c'; self.run_cmd([sys.executable,folder/'tools/gentables.py',tables])
            flags=[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
                '-DDOS_LARGE_WORLD','-DDOS_STREAM_LIGHT_CACHE','-Werror=implicit-function-declaration',
                '-I',folder,'-I',src,'-I',ROOT/'src/platform/dos']
            obj=folder/'hdd.o'
            self.run_cmd([*flags,'-Dopen=world_test_open','-c',ROOT/'src/platform/dos/hdd.c','-o',obj])
            harness=(ROOT/'tests/stream_light_native.c').read_text().replace('u32 g_ramMB=8;',
                'int dos_potato_mode=1,dos_cpu386=1,g_viewDist=4;\nu32 g_ramMB=4;')
            harness=harness.replace('check(argc==2,', 'dos_potato_mode=argc==3 ? atoi(argv[2]) : 0; check(argc==3,')
            harness=harness.replace('"../src/platform/dos/heap.h"','"heap.h"').replace(
                'g_streamLightStats[0] && g_streamLightStats[1] && g_streamLightStats[3]',
                'g_streamLightStats[0] && g_streamLightStats[3]')
            source=folder/'test.c'; source.write_text(harness); exe=folder/'test.exe'
            self.run_cmd([*flags,source,ROOT/'src/platform/dos/heap.c',
                *(src/s for s in ['blocks.c','fmath.c','libc.c']),tables,obj,'-o',exe])
            terrain=folder/'terrain.tmp'
            for mode in [0,1]:
                with self.subTest(mode=mode):
                    shutil.copyfile(ROOT/'build/checks/large-world-proto/terrain1024-lit.bin',terrain)
                    self.assertIn('PASS: canonical baked light',self.run_cmd([exe,terrain,mode]))

    def test_independent_replayable_overlays(self):
        for flag,touched in [('vl_stream_radius','column_cache.inc'),('vl_native_flat','raster.c')]:
            for name in imported.SELECTED:
                original=imported.adapt_source(name,(imported.VENDOR/'src'/name).read_bytes())[0]
                baseline,_=dos_sources.adapt(name,original,**BASE)
                result,recipe=dos_sources.adapt(name,original,**BASE,**{flag:True})
                replay=original
                for step in recipe:
                    before,after=step['before'].encode(),step['after'].encode()
                    self.assertEqual(replay.count(before),step['count'])
                    replay=replay.replace(before,after)
                self.assertEqual(replay,result)
                self.assertEqual(result!=baseline,name==touched)

    @unittest.skipUnless(ZIG.is_file(),'native Zig required')
    def test_exact_actual_packer_unaligned_random_rows_and_runs(self):
        with tempfile.TemporaryDirectory(prefix='dos-next4-pack-') as folder:
            folder=Path(folder)
            code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vga_proven.h"
int dos_very_low_mode=1,dos_potato_mode;
static unsigned char src[321*240],a[76800+32],b[76800+32],proof[200];
static struct DosVgaRun ra[800],rb[800];
int main(void) {
    unsigned seed=1;
    for(int t=0;t<32;++t) {
        for(int i=0;i<sizeof(src);++i) { seed=seed*1664525u+1013904223u; src[i]=seed>>24; }
        memset(a,0xee,sizeof(a)); memset(b,0xee,sizeof(b));
        memset(ra,0,sizeof(ra)); memset(rb,0,sizeof(rb));
        dos_potato_mode=0;
        int na=dos_vga_proven_pack_plan(src+1,321,a+16,76800,proof,ra,800);
        dos_potato_mode=1;
        int nb=dos_vga_proven_pack_plan(src+1,321,b+16,76800,proof,rb,800);
        assert(na==nb && na>=0 && !memcmp(a,b,sizeof(a)) && !memcmp(ra,rb,na*sizeof(*ra)));
        for(int p=0;p<4;++p) for(int y=0;y<240;++y) for(int x=0;x<80;++x)
            assert(b[16+p*19200+y*80+x]==src[1+y*321+x*4+p]);
        for(int i=0;i<16;++i) assert(b[i]==0xee && b[76816+i]==0xee);
    }
    puts("PASS: all four planes, run masks, arbitrary pixel values, unaligned rows and guards");
}
'''
            source=folder/'test.c';source.write_text(code)
            exe=folder/'test.exe'
            self.run_cmd([ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                '-DDOS_VL_PACK_WORDS','-DDOS_VERY_LOW','-DDOS_VGA_PROVEN_VERTICAL',
                '-I',ROOT/'src/platform/dos',source,ROOT/'src/platform/dos/vga_proven.c','-o',exe])
            self.assertIn('PASS:',self.run_cmd([exe]))

    @unittest.skipUnless(ZIG.is_file(),'native Zig required')
    def test_real_flat_raster_guards_and_other_resolution_parity(self):
        with tempfile.TemporaryDirectory(prefix='dos-next4-flat-') as folder:
            folder=Path(folder); hashes=[]
            for enabled in [False,True]:
                out=folder/str(enabled); imported.stage_sources(imported.VENDOR,out)
                src=out/'src'
                for f in src.iterdir():
                    if f.is_file():
                        data,_=dos_sources.adapt(f.name,f.read_bytes(),**BASE,vl_native_flat=enabled)
                        f.write_bytes(data)
                tables=src/'tables.c'
                self.run_cmd([sys.executable,out/'tools/gentables.py',tables])
                exe=out/'test.exe'
                self.run_cmd([ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
                    '-DDOS_VERY_LOW','-DDOS_LOW_PLANAR','-DDOS_VERY_LOW_PLANAR','-DDOS_TEXEL_EDX','-DDOS_ROW_MUL',
                    '-I',src,'-I',ROOT/'src/platform/dos',ROOT/'tests/next4_flat_native.c',
                    src/'raster.c',src/'trap.S',ROOT/'src/platform/dos/low_planar.S',
                    src/'fmath.c',tables,ROOT/'src/platform/dos/graphics_presets.c','-o',exe])
                text=self.run_cmd([exe]); self.assertIn('PASS:',text)
                hashes.append(text.splitlines()[0])
            self.assertEqual(hashes[0],hashes[1])

if __name__=='__main__': unittest.main()
