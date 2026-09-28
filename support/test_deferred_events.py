"""Exercise the actual production producer/drain wrappers with concurrent producers."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('Existing local C++ compiler required.')
snippets = []
for module in ('blectl', 'wifictl', 'timesync'):
    source = (ROOT / 'src/hardware' / (module + '.cpp')).read_text(encoding='utf-8')
    snippets.append('static DeferredEvents ' + module + '_pending_events;')
    for suffix in ('drain_events', 'send_event_cb'):
        name = module + '_' + suffix
        match = re.search(r'^(?:static\s+)?bool\s+' + name + r'\s*\([^;{}]*\)\s*\{', source, re.M)
        assert match, name
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        snippets.append(source[match.start():end])
    assert re.search(r'powermgm_register_loop_cb\(POWERMGM_STANDBY \| POWERMGM_SILENCE_WAKEUP \| POWERMGM_WAKEUP,\s*' + module + '_drain_events', source)
with tempfile.TemporaryDirectory(prefix='xnode-deferred-events-', dir=ROOT / '.pio') as directory:
    scratch = Path(directory)
    (scratch / 'deferred_actual.inc').write_text('\n'.join(snippets), encoding='utf-8')
    binary = scratch / 'deferred_events_test.exe'
    subprocess.run([compiler, '-std=c++11', '-pthread', '-DNATIVE_64BIT', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'src'), '-I' + str(scratch),
                    str(ROOT / 'support/deferred_events_test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
    source = (ROOT / 'src/hardware/ble/gadgetbridge.cpp').read_text(encoding='utf-8')
    snippets = []
    for signature in (r'void onWrite\(NimBLECharacteristic\* pCharacteristic\)',
                      r'static void gadgetbridge_dispatch_pending_connect\(void\)'):
        match = re.search(signature + r'\s*\{', source)
        assert match, signature
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        snippets.append(source[match.start():end])
    (scratch / 'gadgetbridge_actual.inc').write_text('\n'.join(snippets), encoding='utf-8')
    binary = scratch / 'gadgetbridge_receive_test.exe'
    subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I' + str(scratch),
                    str(ROOT / 'support/gadgetbridge_receive_test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
