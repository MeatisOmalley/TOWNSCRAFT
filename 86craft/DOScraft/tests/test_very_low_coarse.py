"""Real overlay replay, planar bounds and deferred mesh scheduling contracts."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported
import dos_sources

ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
BASE = dict(large_world=True, stream_light=True, far_field=True, graphics=True,
    proven_rows=True, raster_finish_skip=True, very_low_texels=True, low_planar=True,
    very_low_planar=True, trap_four=True, flat_inline=True, fill_rows=True, row_pointer=True)


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}\n', start) + 3]


class CoarseTests(unittest.TestCase):
    def test_overlays_are_optional_bounded_and_replayable(self):
        for option, touched in (('very_low_coarse', {'render.c'}),
                                ('very_low_local_mesh', {'world.c', 'game.c'}),
                                ('very_low_collect_margin', {'render.c'})):
            for name in imported.SELECTED:
                original = imported.adapt_source(name, (imported.VENDOR / 'src' / name).read_bytes())[0]
                settings = dict(BASE)
                if option == 'very_low_collect_margin':
                    settings.update(collect_margin=True, collect_swap=True)
                baseline, _ = dos_sources.adapt(name, original, **settings)
                result, recipe = dos_sources.adapt(name, original, **settings, **{option: True})
                replay = original
                for step in recipe:
                    before, after = step['before'].encode(), step['after'].encode()
                    self.assertEqual(replay.count(before), step['count'])
                    replay = replay.replace(before, after)
                self.assertEqual(result, replay)
                self.assertEqual(result != baseline, name in touched, (option, name))

    @unittest.skipUnless(ZIG.is_file(), 'Existing native Zig required')
    def test_actual_far_fill_wide_planar_rows_and_guards(self):
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "far_fill.h"
static unsigned char memory[320*100+32];
int main(void) {
    for(int mode=14;mode<=16;mode+=2)
    for(int pattern=0;pattern<=FAR_PATTERN_GRAIN;++pattern)
    for(int phase=0;phase<2;++phase)
    for(int edge=0;edge<2;++edge) {
        memset(memory,0xee,sizeof(memory));
        int x=edge ? 152 : 0,stride=mode==14 ? 160 : 320;
        int begin=mode==14 ? x/2 : x*2,count=mode==14 ? 4 : 16;
        FarColumn c={memory+16+100*stride+begin,100,stride,mode,2,x};
        far_fill_column(&c,0,7,8,(pattern<<1)|phase);
        assert(c.top==0 && c.cursor==memory+16+begin);
        for(int y=0;y<100;++y) for(int i=0;i<stride;++i) {
            unsigned char value=memory[16+y*stride+i];
            if((mode==16 || !(y&1)) && i>=begin && i<begin+count) assert(value==7 || value==8);
            else assert(value==0xee);
        }
        for(int i=0;i<16;++i) assert(memory[i]==0xee && memory[16+stride*100+i]==0xee);
    }
    puts("PASS: wide planar strips fill all four kept bytes; odd rows and guards untouched");
}
'''
        self.compile_run(code, ['-DDOS_VERY_LOW_PLANAR'])

    @unittest.skipUnless(ZIG.is_file(), 'Existing native Zig required')
    def test_actual_dirty_scheduler_defers_only_offscreen_background_meshes(self):
        original = imported.adapt_source('world.c', (imported.VENDOR / 'src/world.c').read_bytes())[0]
        data, _ = dos_sources.adapt('world.c', original, **BASE, very_low_local_mesh=True)
        scheduler = function(data.decode(), 'int world_update_dirty_chunks(int maxLightOnly)')
        distance = function(data.decode(), 'static int column_dist2(')
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "world.h"
#include "player.h"
#include "graphics_presets.h"
Player g_player;
int g_viewDist=4,dos_cpu386=1,dos_potato_mode=1,g_NC=64,g_W=1024,g_allocChunkCount=6,g_residentColumns=2;
static Chunk storage[6]; Chunk *g_chunks=storage;
static short coords[4]={32,32,36,32},map[4096];
short *g_columnCoords=coords,*g_columnMap=map;
u8 *g_columnReady;
static int anyDirty,cacheAllocated=1,meshJobCx=-1,meshJobCy=-1,meshJobCz=-1;
static int nearRebuild,farRebuild,steps,cancels;
static int cache_light_pending(void) { return 0; }
static void mesh_cancel(void) { ++cancels; meshJobCx=meshJobCy=meshJobCz=-1; }
static void rebuild_chunk(int cx,int cy,int cz,u32 layers) {
    (void)layers;
    ++*(cx==32 ? &nearRebuild : &farRebuild);
    g_chunks[chunk_index(cx,cy,cz)].dirty=0;
}
static void mesh_step(int cx,int cy,int cz) { ++steps; g_chunks[chunk_index(cx,cy,cz)].dirty=0; }
static void reset(int near,int far) {
    memset(storage,0,sizeof(storage));
    memset(map,0xff,sizeof(map)); map[32*64+32]=0; map[32*64+36]=1;
    storage[0].meshed=storage[3].meshed=1;
    storage[0].dirty=near; storage[3].dirty=far;
    nearRebuild=farRebuild=steps=cancels=0;
    anyDirty=1; meshJobCx=meshJobCy=meshJobCz=-1;
    g_player.body.x=(32*CS+8)*FU; g_player.body.z=(32*CS+8)*FU;
    dos_cpu386=dos_potato_mode=1;
}
''' + distance + '\n' + scheduler + r'''
int main(void) {
    reset(DIRTY_LIGHT,DIRTY_STREAM);
    assert(world_update_dirty_chunks(1) && nearRebuild==1 && steps==0);
    assert(storage[3].dirty==DIRTY_STREAM && anyDirty);
    meshJobCx=36; meshJobCy=0; meshJobCz=32;
    assert(!world_update_dirty_chunks(1) && cancels==1 && storage[3].dirty==DIRTY_STREAM);
    g_player.body.x=(36*CS+8)*FU;
    assert(world_update_dirty_chunks(1) && steps==1 && !storage[3].dirty);
    reset(0,DIRTY_GEOMETRY);
    assert(world_update_dirty_chunks(1) && farRebuild==1 && !storage[3].dirty);
    reset(DIRTY_LIGHT,DIRTY_STREAM);
    assert(world_update_dirty_chunks(1000) && nearRebuild==1 && farRebuild==1 && !anyDirty);
    reset(0,DIRTY_LIGHT); dos_cpu386=0;
    assert(!world_update_dirty_chunks(1) && farRebuild==0 && storage[3].dirty==DIRTY_LIGHT);
    reset(0,DIRTY_LIGHT); dos_potato_mode=0;
    assert(world_update_dirty_chunks(1) && farRebuild==1);
    puts("PASS: near work, deferred approach, edit priority, job cancellation, full drain and other tiers");
}
'''
        self.compile_run(code, [])

    @unittest.skipUnless(ZIG.is_file(), 'Existing native Zig required')
    def test_actual_flat_shader_obeys_distance_on_every_cpu_and_resolution(self):
        original = imported.adapt_source('render.c', (imported.VENDOR / 'src/render.c').read_bytes())[0]
        data, _ = dos_sources.adapt('render.c', original, **BASE, very_low_coarse=True)
        text = data.decode()
        corners = text[text.index('static const u8 faceCorner[6]'):]
        corners = corners[:corners.index('\n};') + 3]
        quad = function(text, 'static void draw_quad(int x,int y,int z,int dir,int w,int h,int tex,int light)\n{')
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "world.h"
#include "render.h"
#include "raster.h"
#include "bench.h"
#include "textures.h"
#include "graphics_presets.h"
typedef struct { int c[3]; } V3;
typedef struct { int x,y,z,u,v; } CVert;
#define NEAR_Z 64
#undef TEX_TILE
static unsigned char tiles[NUM_TEXTURES][256];
#define TEX_TILE(t,l) tiles[t]
static V3 TX[40],TY[50],TZ[40];
static int camBX=1,camBY=1,camBZ=1,occCapture,occQuery,occBudget;
int g_distanceMode,g_textureSubdivision=SUBDIV_ADAPTIVE,g_flatDist;
int dos_cpu386,dos_potato_mode;
static u8 texTransparent[NUM_TEXTURES],texAvg[NUM_TEXTURES],skyDarkenCur;
u8 g_shadeLUT[16][256];
static int sqTab[256],splitCalls,paintCalls,paintFlags;
static const u8 *paintTile;
static int box_outside(int a,int b,int c,int d,int e,int f) { return 0; }
static int quad_in_frustum(int x,int y,int z,int dir,int ex,int ez) { return 1; }
int face_light_level(int light,int dir,int darken) { return 2; }
static void project(int x,int y,int z,int *sx,int *sy) { *sx=x; *sy=y; }
static void draw_quad_split(int x,int y,int z,int dir,int ex,int ez,int tex,int light) { ++splitCalls; }
static void draw_spoly(const RVert *v,int n,const u8 *tile,int flags,u8 flat,int near,int far) {
    ++paintCalls; paintTile=tile; paintFlags=flags; assert(flat==9);
}
static void draw_cpoly(const CVert *v,int n,const u8 *tile,int flags,u8 flat) {
    ++paintCalls; paintTile=tile; paintFlags=flags; assert(flat==9);
}
''' + corners + '\n' + quad + r'''
int main(void) {
    for(int i=0;i<40;++i) { TX[i].c[0]=i*256; TX[i].c[2]=256; TZ[i].c[2]=(i+1)*256; }
    for(int i=0;i<50;++i) { TY[i].c[1]=i*256; TY[i].c[2]=256; }
    for(int i=0;i<256;++i) sqTab[i]=i*i;
    g_shadeLUT[2][0]=9;
    for(int cpu=0;cpu<2;++cpu) for(int potato=0;potato<2;++potato) {
        dos_cpu386=cpu; dos_potato_mode=potato; g_flatDist=-1; splitCalls=paintCalls=0;
        draw_quad(0,0,0,DIR_PY,4,4,T_STONE,0);
        assert(!splitCalls && paintCalls==1 && !paintTile && !paintFlags);
        g_flatDist=0; splitCalls=paintCalls=0;
        draw_quad(0,0,0,DIR_PY,4,4,T_STONE,0); assert(splitCalls==1 && !paintCalls);
        g_flatDist=-1; texTransparent[T_LEAVES]=1; splitCalls=paintCalls=0;
        draw_quad(0,0,0,DIR_PY,1,1,T_LEAVES,0);
        assert(!splitCalls && paintCalls==1 && paintTile==tiles[T_LEAVES] && (paintFlags&RP_TRANSPARENT));
        static const int ore[]={T_COAL_ORE,T_IRON_ORE,T_GOLD_ORE,T_DIAMOND_ORE,T_CRAFT_TOP,T_CHEST_FRONT};
        for(int i=0;i<6;++i) {
            paintCalls=0; draw_quad(0,0,0,DIR_PY,1,1,ore[i],0);
            assert(paintCalls==1 && !paintTile);
        }
        g_flatDist=1; splitCalls=paintCalls=0;
        draw_quad(3,0,3,DIR_PY,4,4,T_STONE,0);
        assert(!splitCalls && paintCalls==1 && !paintTile);
        paintCalls=0; draw_quad(1,0,1,DIR_PY,1,1,T_STONE,0);
        assert(paintCalls==1 && paintTile==tiles[T_STONE]);
        g_flatDist=0; paintCalls=0; draw_quad(4,0,4,DIR_PY,1,1,T_STONE,0);
        assert(paintCalls==1 && paintTile==tiles[T_STONE]);
        g_flatDist=-1; occQuery=1; splitCalls=paintCalls=0;
        draw_quad(0,0,0,DIR_PY,4,4,T_STONE,0); assert(splitCalls==1 && !paintCalls);
        occQuery=0;
    }
    puts("PASS: shared zero/positive/all-textured distances, flat subdivision bypass, cutouts and proof passes");
}
'''
        self.compile_run(code, [])

    def compile_run(self, code, flags):
        with tempfile.TemporaryDirectory(prefix='doscraft-vl-coarse-') as folder:
            folder = Path(folder)
            imported.stage_sources(imported.VENDOR, folder / 'imported')
            source = folder / 'test.c'; source.write_text(code)
            exe = folder / 'test.exe'
            result = subprocess.run(list(map(str, [ZIG, 'cc', '-target', 'x86-windows-gnu',
                '-std=gnu99', '-O2', '-UNDEBUG', *flags, '-I', folder / 'imported/src',
                '-I', ROOT / 'src/platform/dos', source, '-o', exe])), capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS:', result.stdout)


if __name__ == '__main__':
    unittest.main()
