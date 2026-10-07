"""Execute production XNODE mesh command bodies against ArduinoJson and fake services.

No BLE peripheral, flash, restart, or RF transmission is performed. Reuse installed
PlatformIO ArduinoJson headers, or set ARDUINOJSON_INCLUDE to its src directory.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('Existing C++ compiler required')
candidates = [Path(os.environ.get('ARDUINOJSON_INCLUDE', ''))] if os.environ.get('ARDUINOJSON_INCLUDE') else []
candidates += list((ROOT / '.pio/libdeps').glob('*/ArduinoJson/src'))
include = next((p for p in candidates if (p / 'ArduinoJson.h').is_file()), None)
if include is None:
    raise SystemExit('ArduinoJson v6 headers required; install firmware dependencies or set ARDUINOJSON_INCLUDE')
source = (ROOT / 'src/hardware/ble/xnode.cpp').read_text()

def function(name):
    match = re.search(r'^\s*(?:bool|void|const char \*|int)\s*' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('Production function missing: ' + name)
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

helpers = source[source.index('        const char *xnode_protocol_id'):source.index('        bool xnode_base64url_encode')]
commands = source[source.index('            const char *protected_reply ='):source.index('            if ( strcmp( type, "setSosConfig" )')]
legacy = source[source.index('            if ( strcmp( type, "setMeshtasticUser" )'):source.index('            if ( strcmp( type, "syncState" )')]
wrapper = '''void xnode_handle_command(DynamicJsonDocument &doc, bool authenticated) {
 const char *type=doc["type"]|"";
 JsonObjectConst payload=doc["payload"].as<JsonObjectConst>();
''' + commands + legacy + '\n}\n'
(ROOT / '.pio').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='xnode-mesh-ble-', dir=ROOT / '.pio') as scratch:
    temp = Path(scratch)
    (temp / 'actual_mesh_ble.inc').write_text(helpers + '\n' + function('xnode_send_hello_ack') + '\n' + wrapper + '\n' + function('xnode_send_meshtastic_rx') + '\n' + function('xnode_send_location_update') + '\n' + function('xnode_send_peer_location') + '\n' + function('xnode_reset_rx') + '\n' + function('xnode_handle_frame') + '\n' + function('xnode_on_disconnect') + '\n' + source[source.index('        class XnodeCallbacks:'):source.index('        XnodeCallbacks xnode_callbacks;')])
    executable = temp / 'mesh_ble_test'
    subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(include), '-I'+str(ROOT / 'src'), '-I'+str(temp),
                    str(ROOT / 'support/mesh_ble_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=20)
    if '--wire-client' in __import__('sys').argv:
        args = __import__('sys').argv
        client_script = args[args.index('--wire-client') + 1]
        subprocess.run(['node', client_script, str(executable)], check=True, timeout=60)

ble = (ROOT / 'src/hardware/ble/meshtastic_ble.cpp').read_text()
for name in ('meshtastic_ble_setup', 'meshtastic_ble_configure_advertising'):
    assert re.search(name+r'\( void \)\s*\{(?:\s*//[^\n]*\n)*\s*if \(mesh_protocol_get_active\(\) != MESH_PROTOCOL_MESHTASTIC\) return', ble), name
assert 'void onWrite( NimBLECharacteristic *pCharacteristic ) {\n                if (mesh_protocol_get_active() != MESH_PROTOCOL_MESHTASTIC) return;' in ble
print('Native Meshtastic GATT setup, advertising and writes are protocol-gated: PASS')

assert 'frame.authenticated = desc->sec_state.encrypted && desc->sec_state.authenticated;' in source
assert 'pCharacteristic->setValue((const uint8_t *)"", 0);' in source
rx_setup = source[source.index('pXnodeRXCharacteristic = pXnodeService->createCharacteristic('):source.index('pXnodeRXCharacteristic->setCallbacks')]
assert 'NIMBLE_PROPERTY::READ' not in rx_setup
assert 'xnode_on_disconnect();' in (ROOT / 'src/hardware/blectl.cpp').read_text()
print('New mutations require authenticated encryption; production fragment context checks pass; RX is write-only and cleared: PASS')

for relative in ('src/app/meshtastic/meshtastic_service.cpp', 'src/app/meshcore/meshcore_service.cpp'):
    backend = (ROOT / relative).read_text()
    assert 'xnode_send_peer_location(' in backend.replace(' (', '('), relative
    assert 'xnode_send_location_update(' not in backend.replace(' (', '('), relative
print('Both radio backends route peer positions through the non-mutating peer helper: PASS')
