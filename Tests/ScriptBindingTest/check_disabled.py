"""Compile changed HTTP/host paths from a scripting-disabled CMake configuration."""
import json
from pathlib import Path
import shlex
import subprocess
import sys

build = Path(sys.argv[1]).resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
if any('/FeiLib/src/Script/' in entry['file'] for entry in commands):
    raise RuntimeError('Disabled build still compiles the script runtime')
for name in ['FeiLib/src/Http/FHttpServer.cpp', 'Server/src/Server/server.cpp',
             'Server/src/Controller/ScriptController.cpp', 'Server/src/Service/ScriptAdminService.cpp']:
    entry = next(item for item in commands if item['file'].endswith('/' + name))
    args = entry.get('arguments') or shlex.split(entry['command'])
    output = build / ('disabled-check-' + Path(name).stem + '.o')
    args[args.index('-o') + 1] = str(output)
    if any('FEILIB_ENABLE_SCRIPT' in argument for argument in args):
        raise RuntimeError('Script definition leaked into the disabled build')
    subprocess.run(args, cwd=entry['directory'], check=True, timeout=120)
    symbols = subprocess.check_output(['nm', '-C', '-u', str(output)], text=True)
    if 'Fei::FScriptEngine' in symbols:
        raise RuntimeError('Disabled host requires script engine symbols: ' + name)
print('PASS disabled scripting: runtime excluded, HTTP server and changed host paths compile without engine dependencies')
