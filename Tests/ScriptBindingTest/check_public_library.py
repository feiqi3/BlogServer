"""Build a consumer using public headers and the actual shared FeiLib only."""
from pathlib import Path
import os
import subprocess
import sys

root=Path(sys.argv[1]).resolve()
build=Path(sys.argv[2]).resolve()
binary=build/'public-script-api-check'
includes=[root/'FeiLib/include',root/'FeiLib/3rd/json/include',root/'FeiLib/3rd/spdlog/include']
subprocess.run(['c++','-std=c++20','-pthread',*[f'-I{path}' for path in includes],
    str(root/'Tests/ScriptBindingTest/engine_test.cpp'),f'-L{build}/Bin/Debug',
    f'-Wl,-rpath,{build}/Bin/Debug','-lFeiLibD','-o',str(binary)],check=True,timeout=120)
environment=os.environ.copy()
environment['LD_LIBRARY_PATH']=f'{build}/Bin/Debug:{build}/debug'
subprocess.run([sys.executable,str(root/'Tests/ScriptBindingTest/run.py'),str(binary)],
    check=True,timeout=30,env=environment)
print('PASS public shared-library API: native bindings, concurrent calls, cancellation and shutdown')
