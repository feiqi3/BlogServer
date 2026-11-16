from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(sys.argv[1]).resolve();build=Path(sys.argv[2]).resolve();binary=build/'script-profiler-test'
includes=[root/'Server/src',root/'FeiLib/include',root/'FeiLib/3rd/json/include',root/'FeiLib/3rd/spdlog/include']
subprocess.run(['c++','-std=c++20','-pthread','-DFEILIB_ENABLE_SCRIPT=1',*[f'-I{x}' for x in includes],
    str(root/'Tests/ArticleHookTest/profiler.cpp'),str(root/'Server/src/Service/ProfilerService.cpp'),
    f'-L{build}/Bin/Debug',f'-Wl,-rpath,{build}/Bin/Debug',f'-Wl,-rpath,{build}/debug','-lFeiLibD','-o',str(binary)],check=True)
with tempfile.TemporaryDirectory(prefix='script-profiling-') as directory:
    subprocess.run([str(binary),directory],check=True,cwd=directory,timeout=20)
