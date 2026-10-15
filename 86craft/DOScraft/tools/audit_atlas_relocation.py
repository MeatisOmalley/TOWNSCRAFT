"""Probe the original pinned raster/assembly for address-sensitive sampling.

No overlay, game changes, emulator or host performance claims. Preserve the
isolated probe executable and its stdout/stderr/result for diagnosis.
"""
import json
import subprocess
from build_game import ROOT
import build_imported as imported


def main():
    output=ROOT/'build/audit/atlas-relocation'
    output.mkdir(parents=True,exist_ok=True)
    imported.stage_sources(imported.VENDOR,output/'imported')
    src=output/'imported/src'
    zig=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
    flags=[str(zig),'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
        '-Wall','-Wextra','-Wno-unused-function','-I',str(src)]
    exe=output/'probe.exe'
    command=[*flags,str(ROOT/'tests/atlas_relocation_probe.c'),str(src/'raster.c'),str(src/'trap.S'),'-o',str(exe)]
    subprocess.run(command,check=True,capture_output=True,text=True,timeout=120)
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    report={'source_commit':imported.PINNED_COMMIT,'command':command,
        'exe_sha256':imported.sha256(exe.read_bytes()),'exit_code':result.returncode,
        'stdout':result.stdout,'stderr':result.stderr,
        'note':'Original native image correctness diagnostic, not a target-speed result or implemented fix.'}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return result.returncode


if __name__=='__main__': raise SystemExit(main())
