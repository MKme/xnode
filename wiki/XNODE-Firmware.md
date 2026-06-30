# XNODE Firmware

The XNODE firmware turns LilyGO watch-class hardware and the T-Deck Plus into compact tactical endpoints for the MKME X stack.

## Main app surfaces

Current user-facing surfaces include:

- Main clock/watchface with moon phase.
- Launcher.
- Messages.
- Mesh compose and receive views.
- Tactical map.
- GPS settings/status diagnostics.
- Alert Summary.
- SOS.
- CheckIn.
- Display, GPS, BLE, WiFi, touch, battery, and other setup pages.

## Tactical actions

SOS and CheckIn are intentionally fast:

- SOS sends a clear XTOC `SITREP` packet over the watch Meshtastic radio with the configured roster Unit ID, destination Unit ID, priority/status fields, current lat/lon, and `Manual SOS` note.
- CheckIn sends a clear XTOC `CHECKIN/LOC` packet with Unit ID, OK status, current lat/lon, and timestamp.

Both actions require:

- Configured watch Unit ID.
- Valid location.
- Ready Meshtastic radio/channel.

## GPS behavior

The firmware uses GPS for:

- User position marker on the tactical map.
- CheckIn/SOS position fields.
- GPS status diagnostics.
- GPS UTC time sync when valid receiver time is available.

Ultra power behavior keeps GPS off by default at idle, then map/status pages can start it as needed. T-Deck Plus defaults GPS on because the larger device is being used as a map/message platform and because it was explicitly brought up to sync time and location without manual GPS enable.

## Map behavior

The map currently uses one installed raster basemap rather than a full multi-tile slippy map engine on the watch. Host tools stream the current basemap into SPIFFS. The firmware persists center, zoom, and projection metadata so markers can be aligned after reboot.

The map renders:

- Local GPS position.
- Shared/external location marker.
- Meshtastic position updates.
- XTOC/XCOM overlay markers.

The current marker set supports team members, mesh nodes, SITREPs, CONTACTs, TASKs, CHECKINs, resources, assets, zones, missions, events, phase lines, Sentinel, and routes.

## Messages and alerts

XTOC/XCOM can push news and alert items to the watch. The main clock screen can show a new-message shortcut, but current behavior removes that shortcut after the user opens the message view. Stored messages remain available from the messages launcher.

## T-Deck Plus keyboard behavior

The T-Deck Plus target disables the on-screen LVGL keyboard and uses the physical keyboard. Printable keys, backspace/delete, enter, and escape are injected into the focused LVGL text area.

## Power behavior

The firmware distinguishes idle power savings from active-use responsiveness:

- Active map/watchface paths use performance mode where needed.
- Standby/hibernate paths release performance mode.
- Ultra starts GPS/WiFi idle paths conservatively while keeping BLE discoverable for XTOC/XCOM sync.
- T-Deck display timeout blanks backlight while keeping LVGL/touch/keyboard active to avoid the previous wake stutter state.

See [Operations and Troubleshooting](Operations-and-Troubleshooting) for field checks.
