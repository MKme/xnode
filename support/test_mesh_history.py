"""Compile and run bounded conversation tests using an existing local C++ compiler."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[1]
(ROOT / '.pio').mkdir(exist_ok=True)
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('Existing local C++ compiler required; no dependency download attempted.')
with tempfile.TemporaryDirectory(prefix='xnode-mesh-history-', dir=ROOT / '.pio') as scratch:
    binary = Path(scratch) / 'mesh_history_test.exe'
    subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'src'), str(ROOT / 'support/mesh_history_test.cpp'),
                    str(ROOT / 'src/app/meshtastic/mesh_history.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    source = (ROOT / 'src/app/meshtastic/meshtastic_service.cpp').read_text(encoding='utf-8')
    snippets = []
    for name in ['meshtastic_write_varint', 'meshtastic_read_varint', 'meshtastic_skip_field',
                 'meshtastic_encode_data_message', 'meshtastic_decode_data_message', 'meshtastic_decode_text_message',
                 'meshtastic_find_channel_slot_for_hash', 'meshtastic_routing_ack', 'meshtastic_handle_rx',
                 'meshtastic_configure_crc', 'meshtastic_service_send_payload_internal', 'meshtastic_service_send_text_internal',
                 'meshtastic_powermgm_event_cb', 'meshtastic_powermgm_loop_cb']:
        match = re.search(r'^\s*(?:static\s+)?(?:bool|size_t|int8_t)\s+' + name + r'\s*\([^;{}]*\)\s*\{', source, re.M)
        if not match:
            raise RuntimeError('Production function not found: ' + name)
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        snippets.append(source[match.start():end])
    (Path(scratch) / 'mesh_service_actual.inc').write_text('\n\n'.join(snippets), encoding='utf-8')
    for radio_major in (6, 7):
        binary = Path(scratch) / ('mesh_service_v' + str(radio_major) + '_test.exe')
        subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Werror',
                        '-DRADIOLIB_VERSION_MAJOR=' + str(radio_major),
                        '-I' + str(ROOT / 'src'), '-I' + scratch,
                        str(ROOT / 'support/mesh_service_test.cpp'),
                        str(ROOT / 'src/app/meshtastic/mesh_history.cpp'), '-o', str(binary)], check=True)
        print('RadioLib', radio_major, 'API branch:', flush=True)
        subprocess.run([str(binary)], check=True)
