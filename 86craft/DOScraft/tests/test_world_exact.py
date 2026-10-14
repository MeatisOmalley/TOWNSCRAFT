"""Exact world/stream overlays: replayable recipes, fail-closed text and host checks.

Pixel identity and timing are guest evidence (tools/benchmark.py image hashes);
these tests only pin the recipes and the two self-contained helpers.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources

BASE=dict(large_world=True,stream_light=True,collect_margin=True,far_field=True,
    low_planar=True,raster_finish_skip=True,proven_rows=True,trap_four=True,very_low_texels=True)
TOUCHES={'stream_validate_once':{'column_store.inc','column_cache.inc'},
    'light_offsets':{'world.c'},'mesh_edges':{'world.c'},'collect_swap':{'render.c'},
    'memmove_dwords':{'libc.c'},'world_scans':{'world.c'},
    'baked_light_fast':{'column_cache.inc'},'sort_histograms':{'render.c'}}
CC=['gcc','-m32','-O2','-march=i486','-std=gnu99','-fno-builtin',
    '-fno-tree-loop-distribute-patterns','-Wall']


def host_i386():
    if not shutil.which('gcc'): return False
    with tempfile.TemporaryDirectory() as directory:
        source=Path(directory)/'probe.c'; source.write_text('int main(void){return 0;}\n')
        return subprocess.run([*CC,str(source),'-o',str(Path(directory)/'probe')],
                              capture_output=True).returncode==0


def staged(name):
    return imported.adapt_source(name,(imported.VENDOR/'src'/name).read_bytes())[0]


def function(text,signature):
    start=text.index(signature)
    return text[start:text.index('\n}\n',start)+3]


class WorldExactTests(unittest.TestCase):
    def test_each_overlay_is_optional_and_replayable(self):
        for option,touched in TOUCHES.items():
            for name in imported.SELECTED:
                original=staged(name)
                base,_=dos_sources.adapt(name,original,**BASE)
                candidate,recipe=dos_sources.adapt(name,original,**BASE,**{option:True})
                replay=original
                for step in recipe:
                    old,new=step['before'].encode(),step['after'].encode()
                    self.assertEqual(replay.count(old),step['count']); replay=replay.replace(old,new)
                self.assertEqual(candidate,replay)
                self.assertEqual(candidate!=base,name in touched,(option,name))
            for name in touched:
                with self.assertRaises(ValueError):
                    dos_sources.adapt(name,b'unexpected',stream_light=True,**{option:True})

    def test_prerequisite_free_builds_skip_dependent_overlays(self):
        for name,option in (('render.c','collect_swap'),('column_cache.inc','baked_light_fast')):
            original=staged(name)
            self.assertEqual(dos_sources.adapt(name,original,**{option:True})[0],
                             dos_sources.adapt(name,original)[0])

    def test_stream_path_hashes_once_and_others_stay_checked(self):
        cache,_=dos_sources.adapt('column_cache.inc',staged('column_cache.inc'),**BASE,
                                  stream_validate_once=True)
        store,_=dos_sources.adapt('column_store.inc',staged('column_store.inc'),**BASE,
                                  stream_validate_once=True)
        cache,store=cache.decode(),store.decode()
        self.assertIn('!column_prepare_unchecked(bx,bz) || !cache_load(bx,bz,slot)',cache)
        self.assertIn('if(!column_valid(cx,cz)) { streamError=1; return 0; }',
                      function(cache,'static int cache_load('))
        self.assertNotIn('column_valid',function(store,'static int column_prepare_unchecked('))
        self.assertIn('if(!column_valid(cx,cz))',function(store,'static int column_prepare('))

    @unittest.skipUnless(host_i386(),'Host gcc -m32 unavailable')
    def test_host_memmove_and_sort(self):
        libc,_=dos_sources.adapt('libc.c',staged('libc.c'),memmove_dwords=True)
        render,_=dos_sources.adapt('render.c',staged('render.c'),**BASE,sort_histograms=True)
        reference=function(staged('render.c').decode(),'static void sort_items(void)')
        candidate=function(render.decode(),'static void sort_items(void)')
        move=function(libc.decode(),'void *memmove(').replace('void *memmove(','void *dos_memmove(')
        program=r'''#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
static u32 itemKey[8000]; static u16 order[8000],order2[8000]; static int nItems;
#define sort_items reference_sort
'''+reference+'''#undef sort_items
'''+candidate+move+r'''
int main(void){
    static u8 a[4096],b[4096]; static u16 want[8000]; u32 seed=1; int fails=0,n,off,dist,i;
    for(off=0;off<8;++off) for(dist=1;dist<40;++dist) for(n=0;n<300;n+=n<40?1:37) {
        for(i=0;i<4096;++i) a[i]=b[i]=(u8)(i*131+7);
        dos_memmove(a+100+off+dist,a+100+off,n); memmove(b+100+off+dist,b+100+off,n);
        fails+=memcmp(a,b,sizeof(a))!=0;
    }
    for(int round=0;round<400;++round) {
        nItems=round*19%8001;
        for(i=0;i<nItems;++i) { seed=seed*1103515245u+12345u; itemKey[i]=(seed>>9)&((round&1)?0x1FFFFF:0x3F); }
        reference_sort(); memcpy(want,order,sizeof(want)); sort_items();
        fails+=memcmp(want,order,nItems*2)!=0;
    }
    printf("%s %d\n",fails ? "FAIL" : "PASS",fails); return fails!=0;
}
'''
        with tempfile.TemporaryDirectory(prefix='doscraft-world-exact-') as directory:
            source=Path(directory)/'world_exact.c'; source.write_text(program)
            exe=Path(directory)/'world_exact'
            subprocess.run([*CC,'-Wno-unused-function',str(source),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=300)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':
    unittest.main()
