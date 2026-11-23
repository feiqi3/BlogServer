"""Build against the real Server ORM and bundled SQLite; inspect actual lifetime calls."""
import json
from pathlib import Path
import shlex
import sqlite3
import subprocess
import sys
import tempfile
root=Path(sys.argv[1]).resolve();build=Path(sys.argv[2]).resolve()
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(item for item in entries if item['file'].endswith('/Server/src/DAO/DataBaseOperation.cpp'))
args=shlex.split(entry['command']);obj=build/'database-lifetime-orm.o'
args[args.index('-o')+1]=str(obj)
subprocess.run(args,cwd=entry['directory'],check=True)
test=build/'database-lifetime-test'
includes=[root/'Server/src',root/'Server/3rd/sqlite3/include',root/'FeiLib/include',root/'FeiLib/3rd/reflect',root/'FeiLib/3rd/json/include',root/'FeiLib/3rd/spdlog/include']
subprocess.run(['c++','-std=c++20','-pthread','-DFEILIB_ENABLE_SCRIPT=1','-DFEI_DEBUG',*[f'-I{x}' for x in includes],str(root/'Tests/ArticleHookTest/database_lifetime.cpp'),str(obj),
    '-Wl,--wrap=sqlite3_open_v2','-Wl,--wrap=sqlite3_close_v2','-Wl,--wrap=sqlite3_finalize',
    f'-L{build}/Bin/Debug',f'-L{build}/debug',f'-Wl,-rpath,{build}/Bin/Debug',f'-Wl,-rpath,{build}/debug','-lFeiLibD','-lsqlite3','-o',str(test)],check=True)
with tempfile.TemporaryDirectory(prefix='hook-database-') as directory:
    database=Path(directory)/'fixture.db'
    with sqlite3.connect(database) as connection:connection.execute('CREATE TABLE Fixture (id INTEGER PRIMARY KEY)')
    subprocess.run([str(test),str(database)],check=True,timeout=20,cwd=directory)
