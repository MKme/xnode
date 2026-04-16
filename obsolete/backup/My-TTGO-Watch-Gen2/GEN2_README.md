# Gen2 Working Copy

Path:
- `C:\GitHub\lilygo\My-TTGO-Watch-Gen2`

Purpose:
- This is the editable follow-on workspace cloned from the frozen Gen1 baseline.
- Use this folder for all Meshtastic, LoRa, UI, and map work.

Baseline source:
- `C:\GitHub\lilygo\My-TTGO-Watch-Gen1`

Rule:
- Keep `Gen1` untouched.
- Make all new experiments, fixes, and feature work here.

## Current Gen2 scope

This build keeps the original My-TTGO-Watch UI and adds Meshtastic radio integration work on top of it.

Implemented in Gen2:
- Existing `mesh chat` app transmit path retained and built.
- Meshtastic receive path decodes the Meshtastic `Data` envelope instead of assuming plain text payloads.
- Text packets are routed into the existing message inbox UI.
- Position packets are decoded and forwarded into the OSM map app as an external marker.
- OSM map coordinate math was fixed so longitude projection uses the correct tile conversion.
- OSM map Wi-Fi autostart setting bug was fixed.
- The original S3 standby/wakeup path was causing black-screen wake failures and reboot loops, so Gen2 no longer uses real standby on this target.
- A display-dark timeout attempt was added after that, but as of this snapshot the user reported that the screen still does not time out as intended.
- The Meshtastic app tile was compacted to reduce label wrapping and button crowding, but this should still be considered an area to refine further.

Primary files changed:
- `src/app/meshtastic/meshtastic_service.cpp`
- `src/app/meshtastic/meshtastic_app.cpp`
- `src/app/osmmap/osmmap_app_main.cpp`
- `src/app/osmmap/osmmap_app_main.h`
- `src/gui/gui.cpp`
- `src/hardware/display.cpp`
- `src/hardware/hardware.cpp`
- `src/utils/osm_map/osm_map.cpp`
- `src/utils/osm_map/osm_map.h`

## Flash target

Hardware this was built and flashed for:
- LilyGO T-Watch S3 / T-Watch 2020 form factor
- ESP32-S3
- 16MB flash
- 8MB PSRAM
- 915MHz SX1262 LoRa

PlatformIO environment:
- `t-watch2020-v3-s3`

## Build

From this folder:

```powershell
pio run -e t-watch2020-v3-s3
```

Generated firmware:
- `.pio\build\t-watch2020-v3-s3\bootloader.bin`
- `.pio\build\t-watch2020-v3-s3\partitions.bin`
- `.pio\build\t-watch2020-v3-s3\firmware.bin`

## Flash method that worked

This flash layout successfully wrote and rebooted on the watch:

```powershell
python C:\Users\Eric\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --port COM8 --baud 460800 write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 .pio\build\t-watch2020-v3-s3\bootloader.bin 0x8000 .pio\build\t-watch2020-v3-s3\partitions.bin 0xE000 C:\Users\Eric\.platformio\packages\framework-arduinoespressif32@3.20009.0\tools\partitions\boot_app0.bin 0x10000 .pio\build\t-watch2020-v3-s3\firmware.bin
```

If the port changes, re-detect the current ESP32-S3 bootloader port and substitute it.

## Boot verification

This Gen2 image was flashed successfully and the watch reported a clean boot log afterwards.

Observed serial behavior after flash:
- Device enumerated on `COM8`
- Firmware initialized display, GUI, SPIFFS, BLE host, and post-setup hardware
- No immediate boot crash or reset loop was observed in the captured serial log

## User-observed runtime state

Confirmed by direct user feedback:
- Watch boots and runs this firmware/UI.
- Meshtastic text sending works.
- The screen no longer immediately falls into the previous black-screen reboot loop path.
- The screen timeout behavior is still not correct; the user reported that it does not time out as intended.

Known user-facing Meshtastic behavior:
- Incoming Meshtastic `TEXT_MESSAGE_APP` packets create inbox notifications.
- Incoming Meshtastic `POSITION_APP` packets also create a notification popup / inbox entry.
- That position popup is expected in the current implementation: it means another node sent a Meshtastic position report.
- When that happens, Gen2 also pushes the reported lon/lat into the OSM map app as a single external marker.
- Current implementation does not yet distinguish position notifications from chat messages in a cleaner UI-specific way.

## Known verification boundary

Confirmed:
- Build completes
- Flash completes
- Firmware boots
- Meshtastic text sending works on-device
- Meshtastic service code compiles with text TX, text RX decode, and position RX decode
- OSM map marker plumbing compiles and is present in firmware

Not yet physically confirmed over-the-air in this document:
- Receiving a live text from another Meshtastic node
- Receiving a live position packet and watching the marker appear in the OSM map app
- End-to-end XTOC plotting workflow
- Stable display timeout-to-dark with touch wake and no reboot

## Why the position report popup appears

Meshtastic uses multiple application port numbers inside its encrypted payloads.

In Gen2:
- Port `1` is treated as a text message.
- Port `3` is treated as a position report.

When a port `3` packet is received, Gen2 currently does two things:
- queues a notification / inbox message such as `Pos <lat> <lon> [alt]`
- updates the OSM map app with a single external marker for that sender

This is useful for later map plotting work, but the notification text is still basic and can be confusing if you are expecting only chat messages.

## Next work items

- Build a real S3 screen timeout-to-dark and touch wake path without returning to reboot loops
- Separate chat notifications from position notifications in the UI
- Verify position packets render reliably in the OSM map app
- Extend from single marker plotting toward packet history / XTOC plotting
- Add better peer labeling and map marker persistence if needed
