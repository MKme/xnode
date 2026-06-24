# System Architecture

XNODE is the embedded edge node inside the MKME X stack. It does not replace XTOC or XCOM. It gives those tools a wrist or handheld endpoint for fast operator actions and map awareness.

## Logical layers

| Layer | Products | Responsibility |
| --- | --- | --- |
| Command and shared picture | XTOC | TOC workflows, roster, packets, map overlays, SATCOM/ATAK/ADS-B, shared workspace, bridges, and exports. |
| Field operator software | XCOM | Offline PWA tools, radio planning, tactical map, field packet workflows, mesh/MANET/APRS/Reticulum/ViperGram transport, XTOC import. |
| Wearable/handheld endpoint | XNODE | Watch/T-Deck UI, GPS, mesh, SOS, CheckIn, message alerts, local map, synced overlays, BLE bridge. |
| Passive intel watch | XINTEL | Local audio transcription, intel rules, ViperGram decode, XTOC event ingest. |
| Local analysis | XCORE | Offline AI summaries, anomaly scans, SITREPs, aircraft pattern checks, packet drafting, XTOC/XCOM context. |

## Data flow

The common flow is:

```text
Field observation
  -> XNODE SOS / CheckIn / mesh / GPS
  -> XCOM or XTOC import / bridge
  -> XTOC packet archive and map overlay
  -> XCOM and XNODE synced tactical picture
  -> XINTEL and XCORE add monitoring and analysis when present
```

The reverse flow is:

```text
TOC action in XTOC
  -> roster, basemap, overlay, alert, or message update
  -> XCOM and/or BLE bridge
  -> XNODE watch/T-Deck display
  -> operator sees marker, alert, message, map, or task context
```

## XNODE BLE bridge

The firmware exposes bridge capabilities for the host tools. The current README lists:

- `sync`
- `location`
- `meshtastic`
- `basemap`
- `mapOverlay`
- `newsNotifications`
- `ble`

These are the host-facing surfaces that let XTOC and XCOM treat the watch as an operational peripheral instead of a standalone toy app.

## Storage model

XNODE stores compact state on device:

- Installed basemap raster in SPIFFS.
- Basemap center/zoom/projection settings.
- Persisted overlay markers in `/spiffs/osmmap/overlays.jsonl`.
- Pushed alerts/news items.
- Meshtastic user/channel configuration.
- Device settings for GPS, WiFi, BLE, display, power, and UI.

The watch is not meant to be the authoritative operations database. XTOC/XCOM own richer operational records; XNODE keeps the field-critical subset.

## Runtime model

XNODE is built on the existing watch UI stack with LVGL, PlatformIO, Arduino ESP32, board-specific HALs, and shared app modules. Platform-specific behavior is selected by PlatformIO environments and compile flags:

- `LILYGO_WATCH_ULTRA`
- `LILYGO_WATCH_S3`
- `LILYGO_T_DECK_PLUS`

The firmware keeps shared app logic where possible and isolates hardware differences in HAL and build configuration.

## Current implementation anchors

- PlatformIO environments: `platformio.ini`
- T-Deck HAL: `src/hardware/tdeck_plus_hal.*`
- T-Watch Ultra HAL: `src/hardware/twatch_ultra_hal.*`
- GPS controller: `src/hardware/gpsctl.*`
- BLE XNODE bridge: `src/hardware/ble/xnode.*`
- Meshtastic service: `src/app/meshtastic/meshtastic_service.cpp`
- Tactical map app: `src/app/osmmap/osmmap_app_main.cpp`
- Regression checks: `support/regression_checks.py`

