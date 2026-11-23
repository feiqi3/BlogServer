"""Optimized standalone instrumentation checks, plus a compiled-out consumer."""
from pathlib import Path
import subprocess
import sys
root=Path(sys.argv[1]).resolve();build=Path(sys.argv[2]).resolve();build.mkdir(parents=True,exist_ok=True)
for enabled in (1,0):
    binary=build/f'profiling-{enabled}-test'
    subprocess.run(['c++','-std=c++20','-O2','-pthread','-D_F_EXPORT=1',f'-DFEILIB_ENABLE_PROFILING={enabled}',f'-I{root}/FeiLib/include',str(root/'Tests/ProfilingTest/test.cpp'),str(root/'FeiLib/src/Profiling/FProfiler.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
