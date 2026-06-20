# XNODE

Git-tracked home for the active LilyGO Watch Gen3 / T-Watch S3 XNODE firmware.

Workspace paths:
- Active project: `C:\GitHub\XNODE`
- Archived legacy generations: `C:\GitHub\XNODE\obsolete\backup`

## Buy the watch

[![LILYGO T-Watch S3 Amazon listing](images/lilygo-t-watch-s3-amazon.jpg)](https://amzn.to/4sHfvgK)

Hardware listing:
- [LILYGO T-Watch S3 on Amazon](https://amzn.to/4sHfvgK)

## Companion software

XNODE is built to work with the MKME X software stack. Add the companion software below to turn the watch into part of a larger offline tactical comms, mapping, and intelligence workflow.

- [XTOC - Tactical Operations Center Software Suite](https://store.mkme.org/product/xtoc-tactical-operations-center-software-suite/) - Offline command-center software for SITREPs, TASKs, CHECKIN/LOC, map overlays, zones, SATCOM, ATAK/KML/CoT workflows, and packet-based field coordination. XTOC directly interfaces with XNODE over the BLE bridge to move maps, markers, locations, alerts, and operational data back and forth.
- [XCOM - Offline Radio Communication Suite](https://store.mkme.org/product/xcom-offline-radio-communication-suite/) - Offline-first radio and mapping toolkit with repeater maps, packet stations, callsign lookup, mesh/Reticulum support, and XTOC data import. XCOM also directly interfaces with XNODE over the BLE bridge so operators can send and receive watch-ready map, alert, location, and packet data.
- [XCORE - Offline Tactical AI Analyst](https://store.mkme.org/product/xcore/) - Local Windows AI analyst for XTOC/XCOM operational data, including AO summaries, anomaly scans, 24-hour SITREPs, aircraft pattern checks, and structured packet drafting.
- [XINTEL - Radio Intel Monitor + Transcription](https://store.mkme.org/product/xintel/) - Local radio-intelligence monitor that transcribes legally receivable audio, watches for keyword/rule hits, decodes ViperGram bursts, and pushes structured intel into the XTOC/XCOM workflow.

## Current status

Working now:
- Builds for `t-watch2020-v3-s3`.
- Flashes to the LilyGO Watch Gen3 / ESP32-S3 target.
- Exposes the XNODE BLE bridge to XTOC and XCOM with `sync`, `location`, `meshtastic`, `basemap`, `mapOverlay`, `newsNotifications`, and `ble` capabilities.
- Adds a Manual SOS launcher tile that sends a clear XTOC `SITREP` packet over the watch's Meshtastic radio, with the roster Unit ID, destination Unit ID, `P1`, `HELP`, current lat/lon, and note `Manual SOS`.
- Adds a CheckIn launcher tile that sends a clear XTOC `CHECKIN/LOC` packet over the watch's Meshtastic radio, with the roster Unit ID, `OK` status, current lat/lon, and timestamp.
- Installs the active XTOC/XCOM tactical map raster as the watch basemap in SPIFFS and selects `offline from watch flash` on the watch.
- Replaces the active basemap cleanly with `clearBasemap` plus a streamed `mapTile` upload, so stale seed or old tiles do not bleed into the current map.
- Persists the installed basemap center, zoom, and projection zoom so the same map returns after reboot.
- Displays local position, shared location, Meshtastic position updates, and XTOC/XCOM overlay markers on the tactical map.
- Supports watch markers for team members, mesh nodes, SITREPs, CONTACTs, TASKs, CHECKINs, resource requests, assets, zones, missions, events, phase lines, Sentinel, and routes.
- Uses host-projected `mapX`/`mapY` marker placement when provided, with Web Mercator lon/lat projection as the fallback.
- Keeps markers aligned while zooming and panning the installed watch-flash basemap.
- Persists synced overlay markers in `/spiffs/osmmap/overlays.jsonl` and reloads them when the map opens or the watch reboots.
- Applies replacement overlay syncs transactionally: existing markers stay visible until the expected replacement batch arrives; an intentional zero-count replacement clears the cache.
- Stores XTOC/XCOM pushed news and alert items in the XNODE alerts app, with the watch-side `show pushed news` toggle.
- Keeps the launcher functions active for messages, mesh, Tac Map, media player, Alert Summary, and watchface manager.
- Inactivity timeout returns the T-Watch S3 build to standby instead of leaving it awake indefinitely.
- T-Watch S3 standby uses the LilyGo `ext1` touch wake path on `BOARD_TOUCH_INT`.
- Display timeout settings use a real `15..300` second range. `300` is five minutes, not a hidden never-sleep mode.

Known limits:
- Watch-flash mode is a single installed raster tile, not a multi-tile slippy engine.
- Zoom is image scaling around the installed tile center.
- Panning is constrained by the visible image bounds.
- The watch keeps up to 96 overlay markers; when full, the oldest marker slot is reused.
- The active watch basemap is one current image, not a stored library of selectable maps.
- SPIFFS is small, so host-side tooling must keep the installed raster compact.
- Manual SOS requires a configured watch Unit ID, a valid watch location, and a ready Meshtastic radio/channel before it can transmit.
- CheckIn requires the same watch Unit ID, valid watch location, and ready Meshtastic radio/channel.

## Watch screens

These XNODE screens show the LilyGO T-Watch S3 firmware in daily use.

| Launcher | Tactical map | XNODE alerts |
| --- | --- | --- |
| <img src="images/xnode/IMG_6597.jpg" alt="XNODE launcher showing messages, mesh, Tac Map, media player, alert summary, and watchface manager apps" width="240"> | <img src="images/xnode/IMG_6590.jpg" alt="XNODE tactical map showing an installed basemap, synced markers, and map controls" width="240"> | <img src="images/xnode/IMG_6592.jpg" alt="XNODE alerts screen showing pushed XTOC news, check-ins, and operator alerts" width="240"> |
| App launcher for messages, mesh, the tactical map, media controls, alert summary, and watchface management. | Tactical map view with the installed basemap, synced XTOC/XCOM markers, zoom controls, and map menu access. | Alert/news view for XTOC-pushed check-ins, operator alerts, and other watch-visible updates. |

## Power management audit (2026-04-18)

Scope:
- Board/environment: `t-watch2020-v3-s3`
- Problem reported: the watch stayed awake, the screen did not time out reliably, and the battery drained quickly.

Root cause found:
- `src/gui/gui.cpp` had a `LILYGO_WATCH_S3` special case that skipped the normal timeout-to-standby request path entirely.
- `src/hardware/touch.cpp` put the S3 touch controller into monitor mode for standby, but used a custom GPIO light-sleep wake path instead of the LilyGo S3 `ext1` touch wake path.
- The S3 touch path also read touch coordinates without first checking the touch interrupt state, which increased the chance of false activity and unnecessary polling.
- The saved display timeout still treated `300` as a hidden "no timeout" value, so older settings could keep the watch awake forever even after the standby path was restored.
- Activity resets relied too much on LVGL inactivity tracking, which did not consistently follow every wake source on the S3 build.

Fix applied:
- Re-enabled timeout-driven `POWERMGM_STANDBY_REQUEST` handling for the S3 build in `src/gui/gui.cpp`.
- Changed S3 standby wake to match the LilyGo library path in `lib/twatchs3_core/src/LilyGoLib.cpp`: `esp_sleep_enable_ext1_wakeup(_BV(BOARD_TOUCH_INT), ESP_EXT1_WAKEUP_ALL_LOW)`.
- Gated S3 touch reads on `watch.getTouched()` before reading coordinates in `src/hardware/touch.cpp`.
- Added a firmware-side display activity timer that is reset by touch, button presses, wake requests, alarms, notifications, and explicit keep-awake flows.
- Changed timeout handling so the persisted user setting is always `15..300` seconds. Temporary keep-awake behavior now uses the internal `DISPLAY_NO_TIMEOUT` override instead of the old magic `300` value.
- Added a legacy config migration so pre-fix `/display.json` files that stored `300` as the old never-sleep value are converted once to `15` seconds on boot and then rewritten.
- Fixed S3 standby wake handoff so a touch wake from light sleep becomes a normal `POWERMGM_WAKEUP_REQUEST` and the display comes back without a reboot.

Files changed for this fix:
- `src/gui/gui.cpp`
- `src/hardware/touch.cpp`
- `src/hardware/display.cpp`
- `src/hardware/display.h`
- `src/hardware/config/displayconfig.cpp`
- `src/hardware/button.cpp`
- `src/hardware/powermgm.cpp`
- `src/gui/splashscreen.cpp`
- `src/gui/quickbar.cpp`
- `src/gui/mainbar/mainbar.cpp`
- `src/gui/mainbar/setup_tile/display_settings/display_setting.cpp`
- `src/gui/mainbar/setup_tile/watchface/watchface_manager_app.cpp`
- `src/gui/mainbar/setup_tile/update/update.cpp`
- `src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp`
- `src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_media.cpp`
- `src/app/alarm_clock/alarm_in_progress.cpp`
- `src/app/wifimon/wifimon_app_main.cpp`
- `src/app/sailing/sailing_setup.cpp`

Build verification:
- Confirmed with:

```powershell
pio run -e t-watch2020-v3-s3
```

Flash verification:
- Last confirmed upload after this fix:

```powershell
pio run -e t-watch2020-v3-s3 -t upload --upload-port COM8
```

## Expected sleep / wake behavior

Display timeout:
- User setting range: `15` to `300` seconds in Display settings.
- `300` means `300 seconds` / `5 minutes`.
- There is no normal hidden never-sleep slider value anymore.

What happens when idle:
- The firmware starts fading the backlight during the last `brightness * 8 ms` before timeout.
- At the default mid brightness that fade is about `1 second`.
- At max brightness that fade is about `2.0 seconds`.
- When the timeout expires, the watch requests standby, turns the display off, and then enters ESP32-S3 light sleep if no other subsystem blocks it.

What counts as activity:
- Touch press.
- Side / power button press.
- Wake requests from notifications, media updates, alarms, splash/update UI, and other explicit wake paths.

Wake methods:
- Touch interrupt on `BOARD_TOUCH_INT` using ESP32 `ext1` wake, matching the LilyGo S3 library.
- Power / side button.
- Motion wake paths already wired through the BMA callback flow.
- RTC alarm / silence wake paths.
- PMU / charger related interrupts.
- Bluetooth notification/media wake when those options are enabled.

Touch wake interaction:
- One touch should wake the watch from standby.
- After wake, the normal next touch interaction should be able to scroll or change screens without forcing a full reboot.

Temporary no-timeout cases:
- Internal app flows can still keep the display awake with `DISPLAY_NO_TIMEOUT`.
- Current users are OTA update, watchface manager, Wi-Fi monitor, and the sailing app's explicit "Always on display" toggle.
- Those are runtime overrides, not saved Display settings.

Follow-up risk still open:
- `powermgm_set_lightsleep(false)` is called in `src/utils/http_ota/http_ota.cpp` and `src/gui/mainbar/setup_tile/battery_settings/battery_calibration.cpp`.
- Those paths do not currently show a matching release call in the same flow, so light sleep can remain disabled until reboot after those operations.
- That does not explain the idle timeout bug on a clean boot, but it is another battery-life issue worth fixing next.

## Repo layout

The active firmware now lives here in git:
- `boards/`
- `data/`
- `images/`
- `lib/`
- `src/`
- `support/`
- `platformio.ini`

Legacy working copies and old generation snapshots were moved under:
- `C:\GitHub\XNODE\obsolete\backup`

That archive is for reference and rollback only. New work should happen in `C:\GitHub\XNODE`.

The repo also vendors the required T-Watch S3 support libraries under:
- `support/twatch-s3-libdeps`

That removes the last build dependency on `C:\GitHub\lilygo`.

## Map install flow

The XNODE watch map path is:

1. XTOC or XCOM fetches one raster tile for a chosen center and zoom.
2. The host clears the active watch basemap state with `clearBasemap`.
3. The host streams the new image over the XNODE bridge with `mapTileBegin` / `mapTile`.
4. The active tile is written to:

```text
/spiffs/osmmap/current.png
```

5. The host sends `installBasemap` with center longitude, latitude, zoom, and projection zoom.
6. The watch persists that manifest and uses the installed tile in `offline from watch flash`.
7. The watch requests overlay sync for the active basemap, then stores synced overlay markers in `/spiffs/osmmap/overlays.jsonl`.

Behavior on the watch:
- zoom in/out scales the installed tile
- directional controls pan around the tile
- long press recenters to the stored map center
- markers are projected with Web Mercator math and stay in the right position as zoom changes
- host-projected marker pixels are used when the host generated the installed map image
- overlay markers persist across map close/open and watch reboot

## Controls in watch-flash mode

- `+` / `-`: zoom the installed image
- directional inputs: pan the current view
- long press center/select: recenter the map

The minimum zoom is clamped so the tile still fills the display frame. The app should never shrink to a tiny image in the middle with no usable controls.

## Files that implement the map fix

- `src/hardware/ble/xnode.cpp`
  - accepts `clearBasemap`, `mapTileBegin`, `mapTile`, `installBasemap`, `syncState`, `overlayBatch`, `packetBatch`, `newsItem`, and location commands
  - creates the watch basemap directory before writing
  - streams PNG chunks into `/spiffs/osmmap/current.png`
  - acknowledges overlay counts back to the host so XTOC/XCOM can verify sync progress
- `src/app/osmmap/config/osmmap_config.cpp`
- `src/app/osmmap/config/osmmap_config.h`
  - persist installed basemap center, zoom, and projection zoom
- `src/app/osmmap/osmmap_app_main.cpp`
  - resolves watch-flash mode to the installed tile
  - scales one image across zoom levels
  - applies pan offsets only in map mode
  - keeps swipe inversion local to the map view
  - stores and restores overlay markers from `/spiffs/osmmap/overlays.jsonl`
  - handles transactional overlay replacement so partial syncs do not wipe visible markers
- `src/utils/osm_map/osm_map.cpp`
- `src/utils/osm_map/osm_map.h`
  - Web Mercator projection helpers for marker placement

## XTOC / XCOM integration

Host-side install support lives in:
- `C:\GitHub\XTOC\xtoc-web\src\pages\XnodePage.tsx`
- `C:\GitHub\XTOC\xtoc-web\src\core\xnodeBridge.ts`
- `C:\GitHub\xcom\xcom\modules\shared\xnode\xnodeBridge.js`
- `C:\GitHub\xcom\xcom\modules\xnode\xnode.js`

These flows now support installing the active raster tile onto the watch using the existing XNODE install path.
They also sync visible tactical map markers to the watch as overlay batches, including packet/check-in style markers and host-projected marker pixels for the currently installed basemap.

Current host behavior:
- clear the watch basemap and overlay cache before installing a replacement map
- stream the active map image to `/spiffs/osmmap/current.png`
- send the basemap manifest with center and projection metadata
- send watch SOS configuration, including the roster-backed `Watch Unit ID` and `SOS To` Unit ID
- push overlay batches after the watch activates the basemap
- keep overlay markers persistent on the watch until a new complete replacement sync arrives
- push XTOC/XCOM news and alerts into the XNODE alerts app

## Manual SOS over Meshtastic

Manual SOS is a watch-side emergency shortcut. It does not send the distress packet back to the BLE host. When the operator opens `SOS` on the watch and taps `SEND SOS`, XNODE builds one clear XTOC `SITREP` packet and passes it to the watch Meshtastic service for transmission on the active Meshtastic channel.

Packet contents:

- packet family: `SITREP` v1
- source: the configured watch `Unit ID`
- destination: configured `SOS To` Unit ID, or `U0` for broadcast
- priority: `P1`
- status: `HELP`
- location: the watch's currently stored latitude/longitude
- note: `Manual SOS`

Real-world uses:

- injured, lost, trapped, or separated operator who cannot stop to use a phone or tablet
- vehicle crew, shelter lead, search team, or marshal who needs a fast distress cue on the same mesh net the TOC is already monitoring
- bad-weather, low-light, gloved, or high-stress conditions where a two-tap watch flow is more reliable than opening a full field app

Setup for success:

1. In `XTOC Team` or the `XCOM` imported roster, give every watch wearer a stable `Unit ID`.
2. In `XTOC -> XNODE` or `XCOM -> XNODE`, connect the watch, choose `Watch Unit ID` from the roster, choose `SOS To`, and click `Save`.
3. The watch stores this assignment in `/xnode.json` and reloads it after reboot. To clear it, reconnect from `XTOC`/`XCOM`, choose `Unassigned / clear saved watch ID`, and click `Save`.
4. Set the watch location from the XNODE page with `Set watch GPS + time`, `Share current GPS once`, or the GPS relay before relying on Manual SOS. Future GPS-equipped watches can provide this directly. Host-set location is also stored so the last known lat/lon survives reboot.
5. On the watch, open the `mesh` app and confirm the status is `Mesh ready` on the expected Meshtastic channel.
6. Send a short test mesh message and confirm the TOC mesh station can receive and auto-import XTOC packet text.

Use in the field:

1. Open the watch launcher.
2. Tap `SOS`.
3. Tap `SEND SOS`.
4. Confirm the watch shows `SOS sent over mesh`.

If it fails:

- `Set the watch Unit ID in XTOC/XCOM first.` means the watch has not received a roster Unit ID.
- `Set the watch location before sending SOS.` means no valid lat/lon has been stored yet.
- `Meshtastic not ready` means the onboard radio did not initialize or no usable channel is active.

The receiving TOC should leave mesh packet auto-decode enabled so the inbound `SITREP` lands in the normal packet store, timeline, triggers, and map workflows.

## CheckIn over Meshtastic

CheckIn is the routine one-button position report. It also transmits from the watch Meshtastic radio, not back through the BLE host.

Packet contents:

- packet family: `CHECKIN/LOC` v1
- source: the configured watch `Unit ID`
- status: `OK`
- location: the watch's currently stored latitude/longitude
- timestamp: current watch time, rounded to packet minutes

Real-world uses:

- shift start, staging arrival, checkpoint arrival, shelter arrival, route departure, vehicle stop, or post-task accountability
- routine "I am here and OK" updates from operators who should not be distracted by a phone screen
- last-known-position breadcrumbs for TOC staff when a field team only has time for a single button press

Use `CheckIn` for routine accountability. Use `SOS` when the operator needs help, safety response, or urgent TOC attention.

Setup is the same as Manual SOS:

1. Assign the watch wearer a stable roster `Unit ID`.
2. In `XTOC -> XNODE` or `XCOM -> XNODE`, connect the watch, choose `Watch Unit ID`, and click `Save`.
3. The watch keeps the saved Unit ID across reboot until `XTOC`/`XCOM` explicitly saves `Unassigned / clear saved watch ID`.
4. Set the watch location from the XNODE page or GPS relay before relying on CheckIn.
5. On the watch, open the `mesh` app and confirm `Mesh ready` on the intended channel.

Use in the field:

1. Open the watch launcher.
2. Tap `CheckIn`.
3. Tap `CHECK IN`.
4. Confirm the watch shows `Check-in sent over mesh`.

The receiving TOC should auto-decode mesh packets so the inbound `CHECKIN/LOC` updates the unit's latest position on the map and in the roster.

## Build

From `C:\GitHub\XNODE`:

```powershell
pio run -e t-watch2020-v3-s3
```

## Flash

Check the active USB port first:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Description, PNPDeviceID
```

Then flash:

```powershell
pio run -e t-watch2020-v3-s3 -t upload --upload-port COM8
```

Last confirmed watch upload in this workspace used `COM8`.

If the watch does not auto-reset into bootloader mode, put it into boot mode manually and rerun the upload command on the current port.

## Quick verification

1. Build and flash from `C:\GitHub\XNODE`.
2. Open XTOC or XCOM and connect to the watch.
3. Load and install a map tile.
4. On the watch, open the map app and use `offline from watch flash`.
5. Confirm:
   - the same image stays loaded while zoom changes
   - the map still fills the screen at maximum zoom-out
   - markers remain visible and aligned
   - markers survive closing/reopening the map and rebooting the watch
   - a new packet/check-in sync updates markers without making existing markers vanish
   - panning moves the viewed area without affecting the rest of the watch UI
