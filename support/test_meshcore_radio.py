"""Compile production MeshCore radio adapter for both bundled RadioLib API branches."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('A local C++ compiler is required')
(ROOT / '.pio').mkdir(exist_ok=True)
source = (ROOT / 'src/app/meshcore/meshcore_service.cpp').read_text()
match = re.search(r'class Radio : public mesh::Radio\s*\{', source)
assert match, 'Production Radio class missing'
depth, end = 1, match.end()
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
with tempfile.TemporaryDirectory(prefix='xnode-meshcore-radio-', dir=ROOT / '.pio') as scratch:
    (Path(scratch) / 'Stream.h').write_text('#pragma once\nclass Stream {};\n')
    (Path(scratch) / 'meshcore_radio_actual.inc').write_text(source[match.start():end] + ' radio;\n')
    for major in (6, 7):
        binary = str(Path(scratch) / f'radio_v{major}_test')
        subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-reorder',
                        '-DRADIOLIB_VERSION_MAJOR=' + str(major), '-I' + scratch,
                        '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'lib/meshcore/src'),
                        str(ROOT / 'support/meshcore_radio_test.cpp'), '-o', binary], check=True)
        print('RadioLib', major, 'MeshCore adapter:', flush=True)
        subprocess.run([binary], check=True)
