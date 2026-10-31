#!/usr/bin/env python3
"""Run inside the existing pinned Linux build container; reuse core static libs."""
from pathlib import Path
import shlex, subprocess, sys
core=Path(sys.argv[1]).resolve(); output=Path(sys.argv[2]).resolve()
build=core/'build/x86_64-linux-gnu/release/src/simplewallet'
flags={}
for line in (build/'CMakeFiles/simplewallet.dir/flags.make').read_text().splitlines():
 if ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
source=Path(__file__).with_name('verify.cpp').resolve();obj=output.with_suffix('.o');output.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(['/usr/bin/g++',*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],'-c',str(source),'-o',str(obj)],check=True)
args=shlex.split((build/'CMakeFiles/simplewallet.dir/link.txt').read_text())
args[args.index('CMakeFiles/simplewallet.dir/simplewallet.cpp.o')]=str(obj)
args[args.index('-o')+1]=str(output)
subprocess.run(args,cwd=build,check=True)
