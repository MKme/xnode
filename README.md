# XNODE

Git-tracked home for the active LilyGO Watch Gen3 / T-Watch S3 XNODE firmware.

Workspace paths:
- Active project: `C:\GitHub\XNODE`
- Archived legacy generations: `C:\GitHub\XNODE\obsolete\backup`

## Buy the watch

[![LILYGO T-Watch S3 Amazon listing](images/lilygo-t-watch-s3-amazon.jpg)](https://amzn.to/4sHfvgK)

Hardware listing:
- [LILYGO T-Watch S3 on Amazon](https://amzn.to/4sHfvgK)

## Current status

Working now:
- Builds for `t-watch2020-v3-s3`.
- Flashes to the LilyGO Watch Gen3 / ESP32-S3 target.
- Inactivity timeout now returns the T-Watch S3 build to standby instead of leaving it awake indefinitely.
- T-Watch S3 standby now uses the LilyGo `ext1` touch wake path on `BOARD_TOUCH_INT`.
- Display timeout settings now use a real `15..300` second range again. `300` is five minutes, not a hidden never-sleep mode.
- Accepts one installed XNODE basemap tile in watch flash.
- Shows that installed tile in the map app without switching to stale old tiles.
- Zoom buttons scale the same installed image instead of loading another map.
- Markers stay aligned with the map image as zoom changes.
- Map panning works in watch-flash mode.
- Map swipe direction is corrected only inside the map view.

Known limits:
- Watch-flash mode is a single installed raster tile, not a multi-tile slippy engine.
- Zoom is image scaling around the installed tile center.
- Panning is constrained by the visible image bounds.

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
2. The host sends the tile over the XNODE bridge as `mapTile`.
3. The tile is written to:

```text
/spiffs/osmmap/<z>/<x>/<y>.png
```

4. The host sends `installBasemap` with center longitude, latitude, and zoom.
5. The watch persists that manifest and uses the installed tile in `offline from watch flash`.

Behavior on the watch:
- zoom in/out scales the installed tile
- directional controls pan around the tile
- long press recenters to the stored map center
- markers are projected with Web Mercator math and stay in the right position as zoom changes

## Controls in watch-flash mode

- `+` / `-`: zoom the installed image
- directional inputs: pan the current view
- long press center/select: recenter the map

The minimum zoom is clamped so the tile still fills the display frame. The app should never shrink to a tiny image in the middle with no usable controls.

## Files that implement the map fix

- `src/hardware/ble/xnode.cpp`
  - accepts `mapTile` uploads
  - creates `/spiffs/osmmap/<z>/<x>` before writing
  - writes PNG chunks into the final flash tile path
- `src/app/osmmap/config/osmmap_config.cpp`
- `src/app/osmmap/config/osmmap_config.h`
  - persist installed basemap center and zoom
- `src/app/osmmap/osmmap_app_main.cpp`
  - resolves watch-flash mode to the installed tile
  - scales one image across zoom levels
  - applies pan offsets only in map mode
  - keeps swipe inversion local to the map view
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
   - panning moves the viewed area without affecting the rest of the watch UI
