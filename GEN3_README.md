# Gen3 Active Working Copy

Path:
- `C:\GitHub\XNODE`

Purpose:
- `Gen1` stays frozen as the old baseline.
- `Gen2` stays frozen as the first Meshtastic snapshot.
- `Gen3` was the active LilyGO T-Watch S3 / XNODE working tree before migration into git.

Archive paths:
- `C:\GitHub\XNODE\obsolete\backup\My-TTGO-Watch-Gen1`
- `C:\GitHub\XNODE\obsolete\backup\My-TTGO-Watch-Gen2`
- `C:\GitHub\XNODE\obsolete\backup\My-TTGO-Watch-Gen3`

Current rule:
- All active development now happens in `C:\GitHub\XNODE`.

## Current Gen3 state

Working now in `C:\GitHub\XNODE`:
- Firmware builds and flashes on the LilyGO Watch Gen3 (`t-watch2020-v3-s3`).
- XNODE can install one basemap tile into watch flash at `/spiffs/osmmap/<z>/<x>/<y>.png`.
- The watch stores the basemap center lon/lat/zoom in `osmmap.json`.
- `offline from watch flash` loads that installed tile and keeps using the same image at every watch zoom.
- Zoom buttons scale the image in place instead of switching to some stale tile from another zoom.
- Marker placement uses Web Mercator math, so the watch position and external markers stay aligned as the image zoom changes.
- Meshtastic text and position handling still work.

Known limitations:
- Watch-flash mode is a fixed single-tile basemap, not a full slippy multi-tile map engine.
- SPIFFS is small, so the installed tile needs to stay compact. Host-side tooling downsizes PNGs when needed.
- Old tiles can still exist in SPIFFS, but the watch now follows the persisted installed tile instead of bouncing to unrelated images.

## What changed

The broken behavior came from two separate problems:

1. XNODE tile upload wrote files without reliably creating `/spiffs/osmmap/<z>/<x>`.
2. The watch map app still treated watch flash like a normal slippy source, so changing zoom could jump to older tiles already in flash.

The fix in this repo is:

- `src/hardware/ble/xnode.cpp`
  - accepts `mapTile` uploads only under `/spiffs/osmmap`
  - creates `/spiffs/osmmap/<z>/<x>` before writing
  - writes uploaded PNG chunks into the final watch-flash tile path
- `src/app/osmmap/config/osmmap_config.cpp`
- `src/app/osmmap/config/osmmap_config.h`
  - persist the installed watch-flash basemap center and zoom
- `src/app/osmmap/osmmap_app_main.cpp`
  - `offline from watch flash` now resolves to the installed center tile path
  - watch zoom changes scale the LVGL image instead of requesting a different `$z/$x/$y`
- `src/utils/osm_map/osm_map.cpp`
  - marker projection now uses proper Web Mercator pixel math
  - fixed the tile longitude helper bug so marker placement stays consistent

## Watch-flash basemap flow

The host apps now use the same simple install path:

1. Pick a center point, currently from browser GPS.
2. Fetch one raster tile for that center and zoom.
3. Compact the PNG if needed.
4. Send it over XNODE as `mapTile` to:

```text
/spiffs/osmmap/<z>/<x>/<y>.png
```

5. Send `installBasemap` with the manifest center lon/lat/zoom.
6. The watch switches to `offline from watch flash`.

After that:
- zoom in/out scales the installed image
- markers stay in the correct place on that image
- the watch no longer flips between unrelated old zoom tiles

## XTOC / XCOM status

Working now:
- `XTOC/xtoc-web/src/pages/XnodePage.tsx` can load one tile from the active raster template and install it on the watch.
- `xcom/xcom/modules/xnode/xnode.js` can do the same thing.
- Both host apps use the XNODE bridge `mapTile` upload plus `installBasemap` manifest flow.

Relevant host-side files:
- `C:\GitHub\XTOC\xtoc-web\src\core\xnodeBridge.ts`
- `C:\GitHub\XTOC\xtoc-web\src\pages\XnodePage.tsx`
- `C:\GitHub\xcom\xcom\modules\shared\xnode\xnodeBridge.js`
- `C:\GitHub\xcom\xcom\modules\xnode\xnode.js`

## Flash target

Hardware:
- LilyGO T-Watch S3 / T-Watch 2020 form factor
- ESP32-S3
- 16 MB flash
- 8 MB PSRAM
- SX1262 LoRa

PlatformIO environment:
- `t-watch2020-v3-s3`

Last confirmed USB port:
- `COM8`
- `USB Serial Device`
- `USB\\VID_303A&PID_1001&MI_00`

## Build

```powershell
pio run -e t-watch2020-v3-s3
```

## Flash

Normal path:

```powershell
pio run -e t-watch2020-v3-s3 -t upload --upload-port COM8
```

That was the path used for the flashed XNODE basemap fix.

Check the port first:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Description, PNPDeviceID
```

Direct `esptool.py` fallback:

```powershell
python C:\Users\Eric\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --port COM8 --baud 460800 write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 .pio\build\t-watch2020-v3-s3\bootloader.bin 0x8000 .pio\build\t-watch2020-v3-s3\partitions.bin 0xE000 C:\Users\Eric\.platformio\packages\framework-arduinoespressif32@3.20009.0\tools\partitions\boot_app0.bin 0x10000 .pio\build\t-watch2020-v3-s3\firmware.bin
```

If auto-reset fails:
1. Put the watch into boot mode manually.
2. Re-run the upload command on the new COM port if it changed.

Recovery note:
- `https://flasher.meshtastic.org/` can recover the ESP32-S3 if needed.
- That restores Meshtastic firmware, not this watch UI firmware.

## Quick operator check

After flashing:
1. Open XTOC or XCOM.
2. Open the `XNODE` page.
3. Connect to the watch.
4. Press `Load map`.
5. Press `Install map on watch`.
6. On the watch, open the map app and select `offline from watch flash` if it is not already active.
7. Press watch zoom in/out and confirm the same image scales while the marker stays aligned.
