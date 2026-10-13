"""Exact raster setup/texel overlays: replayable recipes and host i386 parity.

The parity harnesses link production and candidate raster.c, trap.S and
low_planar.S into one host i386 program. Host speed is never 486 evidence.
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
from test_edge_recip import EXPORTS

BASE=dict(trap_four=True,very_low_texels=True,raster_finish_skip=True,proven_rows=True,low_planar=True)
TOUCHES={'texel_edx':'trap.S','row_mul':'trap.S','row_pointer':'raster.c','flat_inline':'raster.c',
    'mulshift':'fmath.h','planar_expand_words':'raster.c'}
OPTIONS=tuple(TOUCHES)
DEFINES={'texel_edx':'-DDOS_TEXEL_EDX','row_mul':'-DDOS_ROW_MUL'}
CC=['gcc','-m32','-O2','-march=i486','-std=gnu99','-fno-builtin',
    '-fno-tree-loop-distribute-patterns','-Wall','-Wno-unused-function']


def host_i386():
    if not shutil.which('gcc'): return False
    with tempfile.TemporaryDirectory() as directory:
        source=Path(directory)/'probe.c'; source.write_text('int main(void){return 0;}\n')
        return subprocess.run([*CC,str(source),'-o',str(Path(directory)/'probe')],
                              capture_output=True).returncode==0


def staged(name):
    return imported.adapt_source(name,(imported.VENDOR/'src'/name).read_bytes())[0]


def host_symbols(text,prefix,tables=None):
    """DJGPP COFF spells C symbols with '_'; host ELF i386 does not."""
    tables=prefix if tables is None else tables
    text=re.sub(r'\b_(trap_\w+)',lambda m:prefix+m.group(1),text)
    return text.replace('_g_recip14',tables+'g_recip14').replace(
        '_dos_very_low_rows',tables+'dos_very_low_rows')


class RenderExactTests(unittest.TestCase):
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
                self.assertEqual(candidate!=base,name==touched,(option,name))
            with self.assertRaises(ValueError):
                dos_sources.adapt(touched,b'unexpected',**{option:True})

    def test_texel_steps_have_no_operand_size_prefix(self):
        original=staged('trap.S')
        candidate,_=dos_sources.adapt('trap.S',original,**BASE,texel_edx=True,row_mul=True)
        self.assertNotIn(b'addw',candidate)
        self.assertEqual(candidate.count(b'\taddl\t%ebp,%edx\n'),2)  # TEXEL and SKIPTEXEL
        self.assertEqual(candidate.count(b'\tmull\t%ecx'),2)
        self.assertEqual(candidate.count(b'\timull\t%ebx,%eax'),2)  # Prestep keeps EDX live
        planar=(ROOT/'src/platform/dos/low_planar.S').read_text()
        self.assertEqual(planar.count('#ifdef DOS_TEXEL_EDX'),2)
        self.assertEqual(planar.count('#ifdef DOS_ROW_MUL'),2)

    def test_row_pointer_refuses_unproven_trap_wrappers(self):
        with self.assertRaises(ValueError):
            dos_sources.adapt('raster.c',staged('raster.c'),
                              row_pointer=True,front_to_back=True)

    @unittest.skipUnless(host_i386(),'Host gcc -m32 unavailable')
    def test_host_trap_parity(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-exact-trap-') as directory:
            folder=Path(directory); imported.stage_sources(imported.VENDOR,folder/'imported')
            original=(folder/'imported/src/trap.S').read_bytes()
            reference,_=dos_sources.adapt('trap.S',original,**BASE)
            candidate,_=dos_sources.adapt('trap.S',original,**BASE,texel_edx=True,row_mul=True)
            planar=(ROOT/'src/platform/dos/low_planar.S').read_text()
            objects=[]
            for name,text,defines in (('reference_trap',host_symbols(reference.decode(),'reference_',''),[]),
                    ('candidate_trap',host_symbols(candidate.decode(),''),[]),
                    ('reference_planar',host_symbols(planar,'reference_',''),[]),
                    ('candidate_planar',host_symbols(planar,''),list(DEFINES.values()))):
                source=folder/(name+'.S'); source.write_text(text); obj=folder/(name+'.o')
                subprocess.run([*CC,*defines,'-c',str(source),'-o',str(obj)],check=True); objects.append(str(obj))
            exe=folder/'trap_exact'
            subprocess.run([*CC,str(ROOT/'tests/render_exact_trap_test.c'),*objects,'-o',str(exe)],
                           check=True,capture_output=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=600)
            self.assertEqual(result.returncode,0,result.stderr); self.assertIn('PASS',result.stdout)
            print(result.stdout.strip())

    @unittest.skipUnless(host_i386(),'Host gcc -m32 unavailable')
    def test_host_raster_parity(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-exact-raster-') as directory:
            folder=Path(directory); imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'
            options={option:True for option in OPTIONS}
            objects=[]
            renames=[f'-D{n}=reference_{n}' for n in (*EXPORTS,'raster_complete_odd_rows',
                'raster_set_odd_copy_policy','dos_low_planar_rows','dos_very_low_rows')]
            for kind,prefix,extra in (('reference','reference_',{}),('candidate','',options)):
                out=folder/kind; out.mkdir()
                for name in imported.SELECTED:
                    data,_=dos_sources.adapt(name,(src/name).read_bytes(),**BASE,**extra)
                    (out/name).write_bytes(data)
                (out/'trap_host.S').write_text(host_symbols((out/'trap.S').read_text(),prefix))
                (out/'planar_host.S').write_text(host_symbols(
                    (ROOT/'src/platform/dos/low_planar.S').read_text(),prefix))
                flags=[*CC,'-DDOS_VERY_LOW','-DDOS_LOW_PLANAR','-I',str(out),'-I',str(src)]
                for source,defines in (('raster.c',renames if prefix else []),('trap_host.S',[]),
                        ('planar_host.S',[] if prefix else list(DEFINES.values()))):
                    obj=folder/f'{kind}_{source}.o'
                    subprocess.run([*flags,*defines,'-c',str(out/source),'-o',str(obj)],check=True)
                    objects.append(str(obj))
            exe=folder/'raster_exact'
            subprocess.run([*CC,'-I',str(folder/'candidate'),'-I',str(src),
                str(ROOT/'tests/render_exact_raster_test.c'),*objects,'-o',str(exe)],
                check=True,capture_output=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=600)
            self.assertEqual(result.returncode,0,result.stderr); self.assertIn('PASS',result.stdout)
            print(result.stdout.strip())

    @unittest.skipUnless(host_i386(),'Host gcc -m32 unavailable')
    def test_host_mulshift_matches_signed_imul(self):
        import dos_mulshift
        original=staged('fmath.h')
        candidate,_=dos_mulshift.adapt('fmath.h',original)
        def body(data,name):
            text=data.decode().replace('\r\n','\n')
            start=text.index('static inline int mulshift'); end=text.index('/* (a<<s)/b',start)
            return text[start:end].replace('mulshift',name)
        program=('#include <stdio.h>\n#include <limits.h>\ntypedef unsigned int u32;\n'+
            body(original,'old_mulshift')+body(candidate,'new_mulshift')+r'''
static u32 seed=0x1234567u;
static u32 next(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
__attribute__((noinline)) static int old_var(int a,int b,int s) { return old_mulshift(a,b,s); }
__attribute__((noinline)) static int new_var(int a,int b,int s) { return new_mulshift(a,b,s); }
static int same(int a,int b,int s)
{
    return old_mulshift(a,b,4)==new_mulshift(a,b,4) && old_mulshift(a,b,16)==new_mulshift(a,b,16) &&
        old_var(a,b,s)==new_var(a,b,s);
}
int main(void)
{
    static const int edge[]={0,1,-1,2,-2,INT_MAX,INT_MIN,INT_MIN+1,65535,65536,-65536,-65537,
        0x7FFF,-0x8000,123456789,-987654321};
    long i,j,s;
    for(i=0;i<16;++i) for(j=0;j<16;++j) for(s=1;s<32;++s)
        if(!same(edge[i],edge[j],(int)s)) { printf("FAIL %d %d\n",edge[i],edge[j]); return 1; }
    for(i=0;i<4000000;++i)
    {
        int a=(int)next(),b=(int)next();
        if(i&1) a>>=next()%31;
        if(i&2) b>>=next()%31;
        if(!same(a,b,1+(int)(i%31))) { printf("FAIL %d %d\n",a,b); return 1; }
    }
    puts("PASS: mulshift edge and random products"); return 0;
}
''')
        with tempfile.TemporaryDirectory(prefix='doscraft-exact-mulshift-') as directory:
            source=Path(directory)/'mulshift.c'; source.write_text(program)
            exe=Path(directory)/'mulshift'
            subprocess.run([*CC,str(source),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=300)
            self.assertEqual(result.returncode,0,result.stdout); self.assertIn('PASS',result.stdout)


if __name__=='__main__': unittest.main()
