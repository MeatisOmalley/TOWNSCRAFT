from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


def snippet(data,prefix,cached):
    text=data.decode().replace('\r\n','\n')
    start=text.index('static void axis_tables(')
    end=text.index('/* A steep downward view',start)
    body=text[start:end]
    decl='''static V3 tx[258],ty[WH+2],tz[258],*TX=tx,*TY=ty,*TZ=tz;
static V3 AX16[17],AY16[17],AZ16[17];
static u8 visx[258],visy[WH+2],visz[258],*visX=visx,*visY=visy,*visZ=visz;
static int px[5][258],py[5][WH+2],pz[5][258];
static int *PX[5]={px[0],px[1],px[2],px[3],px[4]};
static int *PY[5]={py[0],py[1],py[2],py[3],py[4]};
static int *PZ[5]={pz[0],pz[1],pz[2],pz[3],pz[4]};
static int planeLen[5],rot[3][3],camBX,camBY,camBZ,tabX0,tabX1,tabZ0,tabZ1;
static int vw,vh,focal16;
'''
    if cached: decl+='static int cameraReuseValid,cameraReuseView,cameraReuseWidth,cameraReuseScale;\nstatic Camera cameraReusePose;\n'
    # Generated harness renames only symbols, never function arithmetic.
    names=set(re.findall(r'\b(?:TX|TY|TZ|AX16|AY16|AZ16|visX|visY|visZ|PX|PY|PZ|planeLen|rot|camBX|camBY|camBZ|tabX0|tabX1|tabZ0|tabZ1|vw|vh|focal16|axis_tables|setup_camera|cameraReuseValid|cameraReuseView|cameraReuseWidth|cameraReuseScale|cameraReusePose|tx|ty|tz|visx|visy|visz|px|py|pz)\b',decl+body))
    functions='''void clear(void) {
memset(tx,99,sizeof(tx));memset(ty,99,sizeof(ty));memset(tz,99,sizeof(tz));
memset(visx,99,sizeof(visx));memset(visy,99,sizeof(visy));memset(visz,99,sizeof(visz));
memset(px,99,sizeof(px));memset(py,99,sizeof(py));memset(pz,99,sizeof(pz));
'''+('cameraReuseValid=0;\n' if cached else '')+'''}
void update_projection(void) { vw=320/g_renderScale; vh=200/g_renderScale; focal16=172*16/g_renderScale; }
'''
    names.add('clear'); names.add('update_projection')
    pattern=re.compile(r'\b('+'|'.join(sorted(names,key=len,reverse=True))+r')\b')
    result=pattern.sub(lambda m:prefix+'_'+m[0],decl+body+functions)
    # The game updates projection before each setup call; preserve that order.
    result=result.replace(prefix+'_setup_camera(void)',prefix+'_setup_body(void)')
    result+=f'void {prefix}_setup_camera(void) {{ {prefix}_update_projection(); {prefix}_setup_body(); }}\n'
    return result


class CameraTests(unittest.TestCase):
    def test_exact_setup_tables_and_reset_invalidation(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-camera-') as folder:
            folder=Path(folder); imported.stage_sources(imported.VENDOR,folder/'imported')
            data=(imported.VENDOR/'src/render.c').read_bytes()
            candidate,recipe=dos_sources.adapt('render.c',data,camera_reuse=True)
            replay=data
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count']);replay=replay.replace(old,new)
            self.assertEqual(replay,candidate)
            self.assertIn('cameraReuseValid=0;',candidate.decode())
            (folder/'camera_reference.inc').write_text(snippet(data,'reference',False))
            (folder/'camera_candidate.inc').write_text(snippet(candidate,'candidate',True))
            tables=folder/'tables.c'
            subprocess.run([sys.executable,str(imported.VENDOR/'tools/gentables.py'),str(tables)],check=True)
            exe=folder/'test.exe'
            command=[str(ZIG),'cc','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wall','-Wextra',
                '-I',str(folder),'-I',str(folder/'imported/src'),str(ROOT/'tests/camera_reuse_test.c'),
                str(folder/'imported/src/fmath.c'),str(tables),'-o',str(exe)]
            process=subprocess.run(command,capture_output=True,text=True,timeout=120)
            self.assertEqual(process.returncode,0,process.stderr)
            process=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
            self.assertEqual(process.returncode,0,process.stderr)
            self.assertIn('PASS: 12000 exact camera/table comparisons',process.stdout)


if __name__=='__main__':unittest.main()
