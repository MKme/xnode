"""Render actual main_tile.cpp through locally installed LVGL, without hardware.

Only hardware/services are stubbed. No network/dependency installation is performed.
Temporary compilation artifacts are removed automatically. Requires clang and Pillow.
"""
from pathlib import Path
import concurrent.futures
import os
import shutil
import subprocess
import tempfile
import hashlib
import json
import argparse
import re

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'site/images/home-tactical-2026-09-28'
LVGL = ROOT / '.pio/libdeps/tdeck-plus/lvgl'

def run(args):
    p = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(p.stdout + p.stderr)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only',action='store_true',help='Run real LVGL and interaction assertions without generating PNGs; no Pillow needed.')
    parser.add_argument('--navigation',action='store_true',help='Validate actual mainbar/app/setup navigation instead of the focused home test.')
    parser.add_argument('--messages',action='store_true',help='Validate actual message-open function bodies with home pointer input and real mainbar navigation.')
    parser.add_argument('--mesh',action='store_true',help='Validate actual Mesh Chat UI, history and keyboard with native LVGL.')
    args=parser.parse_args()
    if not args.check_only:
        from PIL import Image
    if not (LVGL / 'lvgl.h').exists():
        raise SystemExit('Existing local PlatformIO LVGL dependency missing; no download attempted.')
    if not args.check_only:
        OUT.mkdir(parents=True, exist_ok=True)
    cc = shutil.which('clang') or shutil.which('gcc')
    cxx = shutil.which('clang++') or shutil.which('g++')
    if not cc or not cxx:
        raise SystemExit('Existing C/C++ compiler required; no download attempted.')
    screenshots = []
    with tempfile.TemporaryDirectory(prefix='xnode-home-capture-', dir=ROOT / '.pio') as scratch:
        temp = Path(scratch)
        stubs = temp / 'stubs'
        stubs.mkdir()
        (stubs / 'config.h').write_text('#pragma once\n#include "lvgl.h"\n#define RES_X_MAX 540\n#define THEME_PADDING 8\n#define USE_EXTENDED_CHARSET CHARSET_CYRILLIC\n')
        headers = ['gui/mainbar/mainbar.h', 'gui/widget_styles.h', 'gui/widget_factory.h',
                   'gui/mainbar/app_tile/app_tile.h', 'gui/mainbar/note_tile/note_tile.h',
                   'gui/mainbar/setup_tile/setup_tile.h', 'hardware/timesync.h', 'hardware/powermgm.h',
                   'hardware/pmu.h', 'hardware/sensor.h', 'hardware/blectl.h', 'hardware/wifictl.h',
                   'utils/alloc.h', 'utils/logging.h', 'utils/io.h', 'hardware/callback.h', 'app/osmmap/osmmap_app.h', 'app/meshtastic/meshtastic_app.h',
                   'gui/keyboard.h','gui/statusbar.h','gui/gui.h','hardware/display.h','hardware/rtcctl.h','hardware/button.h','hardware/touch.h','hardware/motor.h','hardware/tdeck_plus_hal.h','Preferences.h','gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h']
        for header in headers:
            path = stubs / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#pragma once\n')
        if args.messages:
            source=(ROOT/'src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp').read_text()
            snippets=[]
            for name in ['bluetooth_message_dismiss_main_widget','bluetooth_message_mark_read','bluetooth_message_activate_cb','bluetooth_message_hibernate_cb','bluetooth_message_open_latest','enter_bluetooth_messages_cb']:
                match=re.search(r'^static\s+(?:void|bool)\s+'+name+r'\s*\([^;{}]*\)\s*\{',source,re.M)
                if not match: raise RuntimeError('Actual message function missing: '+name)
                depth=1;end=match.end()
                while depth:
                    depth+=(source[end]=='{')-(source[end]=='}');end+=1
                snippets.append(source[match.start():end])
            (stubs/'message_workflow.inc').write_text('\n\n'.join(snippets))
        common = ['-DNATIVE_64BIT', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE', '-DLV_HOR_RES_MAX=540', '-DLV_VER_RES_MAX=960',
                  '-DMALLOC=malloc','-DREALLOC=realloc',
                  '-I'+str(stubs), '-I'+str(ROOT/'lib'), '-I'+str(LVGL), '-I'+str(LVGL/'src'), '-I'+str(ROOT/'src')]
        sources = list((LVGL/'src').rglob('*.c')) + list((ROOT/'src/gui/font').glob('Ubuntu_cyrillic_*px.c'))
        if args.navigation or args.messages or args.mesh:
            sources += list((ROOT/'src/gui/png_decoder').glob('*.c'))
            sources += list((ROOT/'src/gui/images').glob('info_*16px.c'))
            sources += [ROOT/'src/gui/images'/name for name in ['message_64px.c','gps_64px.c','wifi_64px.c','bluetooth_64px.c','location_64px.c','notification_64px.c','brightness_64px.c','time_64px.c','battery_icon_64px.c','sound_64px.c','style_64px.c','move_64px.c','sdcard_settings_64px.c','utilities_64px.c']]
            sources += [ROOT/'src/app/osmmap/images/osm_64px.c']
            sources += [ROOT/p for p in ['src/app/gps_status/images/gps_status_64px.c','src/app/calc/images/calc_app_64px.c','src/app/weather/images/owm01d_64px.c','src/app/compass/images/compass_64px.c','src/app/stopwatch/images/stopwatch_app_64px.c','src/app/tracker/images/tracker_64px.c','src/gui/mainbar/setup_tile/watchface/images/watchface_64px.c']]
        objects = [temp/(str(i)+'.o') for i in range(len(sources))]
        def compile_one(pair):
            source, obj = pair
            run([cc, '-c', '-O1', *common, source, '-o', obj])
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            list(pool.map(compile_one, zip(sources, objects)))
        targets = [('tdeck-plus','LILYGO_T_DECK_PLUS',320,240),
                   ('watch-ultra','LILYGO_WATCH_ULTRA',362,440),
                   ('watch-s3','LILYGO_WATCH_S3',240,240),
                   ('tdeck-pro','LILYGO_T_DECK_PRO',240,320)]
        for name, define, width, height in targets:
            exe = temp/(name+'.exe')
            run([cxx, '-std=c++17', '-O1', *common, '-D'+define, HERE/('mesh.cpp' if args.mesh else 'messages.cpp' if args.messages else 'navigation.cpp' if args.navigation else 'harness.cpp'), *objects, '-o', exe])
            for theme in ['dark','light']:
                ppm = temp/(name+'-'+theme+'.ppm')
                run([exe,width,height,theme,ppm])
                if args.mesh:
                    run([exe,width,height,theme,ppm,'legacy-examples-on'])
                if args.mesh:
                    if not args.check_only:
                        evidence=ROOT.parent/'vault/xnode-mesh-chat-2026-09-28'
                        evidence.mkdir(parents=True,exist_ok=True)
                        for frame in temp.glob(ppm.name+'-*.ppm'):
                            suffix=frame.name[len(ppm.name)+1:-4]
                            with Image.open(frame) as img:
                                img.save(evidence/(name+'-'+theme+'-'+suffix+'.png'))
                    print('PASS Mesh Chat',name,theme)
                    continue
                if args.messages:
                    print('PASS actual message workflow',name,theme)
                    continue
                if args.navigation:
                    if not args.check_only:
                        evidence=ROOT.parent/'vault/xnode-home-tactical-2026-09-28'
                        evidence.mkdir(parents=True,exist_ok=True)
                        for screen in ['apps','setup']:
                            with Image.open(str(ppm)+'-'+screen+'.ppm') as img:
                                img.save(evidence/(name+'-'+theme+'-'+screen+'.png'))
                    print('PASS navigation',name,theme)
                    continue
                if not args.check_only:
                    with Image.open(ppm) as img:
                        img.save(OUT/(name+'-'+theme+'.png'))
                screenshots.append(dict(file=name+'-'+theme+'.png',width=width,height=height,theme=theme,target=define))
                print('PASS', name, theme)
    if args.mesh:
        if not args.check_only:
            evidence=ROOT.parent/'vault/xnode-mesh-chat-2026-09-28'
            paths=['src/app/meshtastic/meshtastic_app.cpp','src/app/meshtastic/mesh_history.cpp','src/app/meshtastic/mesh_history.h','src/gui/keyboard.cpp','support/home_capture/mesh.cpp','support/home_capture/capture.py']
            record=dict(method='actual-source-native-LVGL',hardware_capture=False,
                        scope='Actual Mesh Chat UI, bounded history, mainbar navigation and software keyboard; controlled radio/service and NVS fixtures. Does not establish RF exchange or persistent-device NVS behavior.',
                        checks=['production starts live with no examples control or preview','legacy saved examples ON ignored in fresh process','channel drafts remain separate, including external channel change','real pointer dropdown with sparse channel slots 0 and 3','failed send retains draft; accepted send clears draft; outgoing statuses have no suffix and only acknowledged records show a checkmark','24-entry rollover preserves actual visible message text and within-card pixel offset','new-history button restores latest view','long received message renders and vertical scroll works','horizontal pointer swipes over chat and radio contents and footer, both directions','real watch/Pro keyboard pointer typing, page changes, OK/Cancel without radio fallthrough','watch keyboard inherits 80-character draft limit and multiline mode; handheld retains one-line mode','watch separate reading/review/radio pages, full-width primary buttons and retained drafts on Back','all watch Mesh buttons meet minimum 48px S3 / 72px Ultra height','radio content has no horizontal overflow','navigation retains mode/history/draft'],
                        source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths},
                        variants=[dict(target=t[0],width=t[2],height=t[3],themes=['dark','light']) for t in targets])
            (evidence/'native-mesh-evidence.json').write_text(json.dumps(record,indent=2)+'\n')
        print('Actual Mesh Chat native LVGL checks passed across four boards and two themes; temporary compilation artifacts removed.')
        return
    if args.messages:
        if args.check_only:
            return
        evidence=ROOT.parent/'vault/xnode-home-tactical-2026-09-28'
        evidence.mkdir(parents=True,exist_ok=True)
        paths=['src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp','src/gui/mainbar/mainbar.cpp','src/gui/mainbar/main_tile/main_tile.cpp','src/gui/mainbar/main_tile/home_tactical.h','src/gui/widget.cpp','support/home_capture/messages.cpp']
        record=dict(method='actual-source-message-entry-functions-home-widget-mainbar',hardware_capture=False,
                    scope='Actual message entry/open-latest/mark-read/dismiss/activation/hibernation function bodies extracted verbatim at build time. Real home pointer, widget and mainbar. Message data/content renderer, button configuration and fade effects isolated as test fixtures.',
                    source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths},
                    checks=['old pre-navigation-mark-read plus historical loop guard reproduces disappearing row without navigation','current rejected navigation retains unread row',
                            'real pointer reaches actual allocated messages tile then clears unread row','latest entry selected','three repeated open/back cycles and reopen while already active'],
                    variants=[dict(target=t[0],themes=['dark','light']) for t in targets])
        (evidence/'native-message-workflow-evidence.json').write_text(json.dumps(record,indent=2)+'\n')
        return
    if args.navigation:
        if not args.check_only:
            evidence=ROOT.parent/'vault/xnode-home-tactical-2026-09-28'
            paths=['src/gui/mainbar/mainbar.cpp','src/gui/mainbar/mainbar.h','src/gui/mainbar/app_tile/app_tile.cpp','src/gui/mainbar/setup_tile/setup_tile.cpp',
                   'src/gui/app.cpp','src/gui/setup.cpp','src/gui/tactical_icons.cpp','src/gui/widget_styles.cpp','support/home_capture/navigation.cpp']
            record=dict(method='actual-source-native-LVGL',hardware_capture=False,
                        scope='Real mainbar, Apps/Setup tile builders, icon registration, theme and icon renderer. Selected existing registration labels/art; application bodies, physical touch controller and global statusbar excluded.',
                        source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths},
                        checks=['horizontal root swipes both directions across occupied menu pages','left/right hardware-button routing','horizontal flattened 2x2 app submenu with retained numeric IDs',
                                'app/setup icon/card/caption/margin pointer clicks','late QuickGLUI-style callback registration preserving original icon object','jump/back',
                                'swipe activation/hibernation callbacks','runtime registration across page boundary retains active private app and remaps saved Setup back target without callbacks',
                                'root/private group boundary and vertical swipe isolation','bounded repeated-jump history','invalid/coordinate-wrap allocation guard','two-line app captions clear page footer'],
                        variants=[dict(target=t[0],width=t[2],height=t[3],themes=['dark','light']) for t in targets])
            (evidence/'native-navigation-evidence.json').write_text(json.dumps(record,indent=2)+'\n')
        print('Actual mainbar/app/setup navigation validated; temporary compilation artifacts removed.')
        return
    sources = ['src/gui/mainbar/main_tile/main_tile.cpp','src/gui/mainbar/main_tile/home_tactical.h','src/gui/mainbar/main_tile/home_clock_font.h','src/gui/mainbar/main_tile/home_moon_texture.h','src/gui/widget_styles.cpp','src/gui/widget_styles.h','support/home_capture/harness.cpp']
    manifest = dict(method='native-lvgl', synthetic_state=True, hardware_capture=False,
                    scope='Actual firmware home via LVGL software framebuffer; global statusbar excluded. Hardware/services replaced with deterministic stubs.',
                    demonstration_state='28 September 2026, 14:32 UTC, radios off, one message shortcut; no physical radio activity implied. Battery remains the global statusbar responsibility; that separate UI is not rendered or validated here.',
                    source_sha256={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources},
                    checks=['real LVGL pointer press/release on visible navigation labels and edges','real LVGL pointer press/release on Messages label/envelope/badge/four edges with original handler and destination spy',
                            'original glued-clickable-label fault injection reproduces swallowed tap (after untouched-production pointer check)','map/mesh/apps/setup callback routing','hardware right button routing',
                            'Wi-Fi/BLE off/on/connected transitions','home duplicate battery and local-time label absent','invalid clock warning retained','sensor visibility','12/24-hour clock',
                            'real shared background/mainbar/app/opaque-app/setup palette','legacy bound surface follows repeated light/dark switches','real home style callback after repeated theme changes'],
                    screenshots=screenshots)
    if not args.check_only:
        evidence = ROOT.parent/'vault/xnode-home-tactical-2026-09-28'
        evidence.mkdir(parents=True,exist_ok=True)
        (evidence/'native-capture-evidence.json').write_text(json.dumps(manifest,indent=2)+'\n')
        public_path=OUT/'manifest.json'
        public=json.loads(public_path.read_text()) if public_path.exists() else dict(description='Firmware screen rendering with illustrative readings')
        native_names={s['file'] for s in screenshots}
        preserved=[s for s in public.get('screenshots',[]) if s.get('file') not in native_names and (OUT/s.get('file','')).is_file()]
        public['screenshots']=screenshots+preserved
        public_path.write_text(json.dumps(public,indent=2)+'\n')
    print('Actual LVGL source renders; synthetic state. Temporary compilation artifacts removed.')

if __name__ == '__main__':
    main()
