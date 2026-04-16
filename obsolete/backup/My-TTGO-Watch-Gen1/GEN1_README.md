# Gen1 Baseline

Created: 2026-04-15

Path:
- `C:\GitHub\lilygo\My-TTGO-Watch-Gen1`

Purpose:
- This is the frozen working baseline for the LilyGO T-Watch S3 / 915 MHz LoRa watch.
- It is based on `sharandac/My-TTGO-Watch` with a local ESP32-S3 port and an in-firmware Meshtastic integration.
- Do not edit this copy. Use `C:\GitHub\lilygo\My-TTGO-Watch-Gen2` for all further work.

Base repo state:
- Upstream repo commit: `953243e`
- This baseline includes local uncommitted changes and new files on top of that commit.
- The `.pio` build output is intentionally preserved in this snapshot.

Target hardware:
- LilyGO T-Watch S3
- ESP32-S3, 16 MB flash, 8 MB OPI PSRAM
- LoRa SX1262, US / 915 MHz
- Tested device MAC: `dc:b4:d9:12:fd:50`

What this snapshot is:
- Original UI and firmware flow from `My-TTGO-Watch`
- Local S3 support added for the T-Watch S3 hardware
- Meshtastic support added inside this firmware instead of replacing the UI with stock Meshtastic firmware

Current status at freeze time:
- Builds successfully with PlatformIO environment `t-watch2020-v3-s3`
- Flashes successfully to the watch
- Boots into the app firmware
- PMU, display, touch, RTC, motor, and radio init paths run
- A `mesh chat` app is added to the watch UI
- Meshtastic radio code is wired into the watch firmware for public LongFast text messages
- Existing watch message inbox integration is present for received Meshtastic text messages

What is implemented:
- ESP32-S3 board definition and PlatformIO environment
- Local LilyGO S3 core integration
- S3-specific boot and hardware bring-up changes
- Meshtastic service layer in:
  - `src/app/meshtastic/meshtastic_service.cpp`
  - `src/app/meshtastic/meshtastic_service.h`
- Meshtastic UI app in:
  - `src/app/meshtastic/meshtastic_app.cpp`
  - `src/app/meshtastic/meshtastic_app.h`
- Existing message inbox hook in:
  - `src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp`
  - `src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h`

Meshtastic behavior in this baseline:
- Uses the watch SX1262 radio
- Configured for public LongFast defaults on US 915 MHz
- Uses the public default channel PSK
- Sends broadcast text messages
- Receives text messages and pushes them into the watch inbox UI
- Uses the watch node ID derived from `ESP.getEfuseMac()`

Meshtastic scope in this baseline:
- Implemented:
  - Public LongFast text send/receive path
  - Basic app UI for send/inbox/status
  - AES-CTR payload handling for public channel traffic
  - LoRa header encode/decode path for text messages
- Not confirmed here:
  - Extended field testing with multiple live mesh peers
  - Private channels and channel configuration UI
  - Full Meshtastic protobuf coverage beyond text messaging
  - Routing, telemetry, admin/config packets, node database, GPS sharing, or map features

Important known runtime note:
- After a fresh full flash, SPIFFS may be empty and the firmware may format SPIFFS on first boot.
- Serial logs during first boot showed the firmware reaching app init, failing initial SPIFFS mount, then entering the format path.

Build command:
```powershell
pio run -e t-watch2020-v3-s3
```

Built artifacts:
- `C:\GitHub\lilygo\My-TTGO-Watch-Gen1\.pio\build\t-watch2020-v3-s3\bootloader.bin`
- `C:\GitHub\lilygo\My-TTGO-Watch-Gen1\.pio\build\t-watch2020-v3-s3\partitions.bin`
- `C:\GitHub\lilygo\My-TTGO-Watch-Gen1\.pio\build\t-watch2020-v3-s3\firmware.bin`

Flash method used successfully for this baseline:
1. Put the watch into boot mode.
2. Wait for the ESP32-S3 bootloader USB port to enumerate as `VID_303A&PID_1001`.
3. Write bootloader, partitions, `boot_app0`, and app image.

Direct flash command used:
```powershell
python C:\Users\Eric\.platformio\packages\tool-esptoolpy\esptool.py `
  --chip esp32s3 `
  --port COM8 `
  --baud 460800 `
  write_flash -z `
  --flash_mode dio `
  --flash_freq 80m `
  --flash_size 16MB `
  0x0 .pio\build\t-watch2020-v3-s3\bootloader.bin `
  0x8000 .pio\build\t-watch2020-v3-s3\partitions.bin `
  0xE000 C:\Users\Eric\.platformio\packages\framework-arduinoespressif32@3.20009.0\tools\partitions\boot_app0.bin `
  0x10000 .pio\build\t-watch2020-v3-s3\firmware.bin
```

Important flashing notes:
- Normal app mode and bootloader mode may enumerate on different COM ports.
- The stable bootloader USB identity observed during successful flashing was `VID_303A&PID_1001`.
- Normal runtime USB serial was also observed as `VID_303A&PID_1001` / `COM8` after successful boot.
- A different runtime enumeration `VID_303A&PID_0002` / `COM9` was also seen during recovery/testing.
- If flashing fails because the port disappears, catch the bootloader port immediately after entering boot mode and flash without extra probing.

Files and directories added by the local S3 / Meshtastic work:
- `boards/`
- `lib/twatchs3_core/`
- `src/app/meshtastic/`
- `src/compat/`
- `src/lilygo_s3_link.cpp`

Main local files changed in this baseline:
- `platformio.ini`
- `src/config.h`
- `src/hardware/hardware.cpp`
- `src/hardware/display.cpp`
- `src/hardware/framebuffer.cpp`
- `src/hardware/framebuffer.h`
- `src/hardware/gpsctl.cpp`
- `src/hardware/motion.cpp`
- `src/hardware/motor.cpp`
- `src/hardware/pmu.cpp`
- `src/hardware/powermgm.cpp`
- `src/hardware/rtcctl.cpp`
- `src/hardware/touch.cpp`
- `src/hardware/wifictl.cpp`
- `src/gui/splashscreen.cpp`
- `src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp`
- `src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h`
- `src/utils/webserver/webserver.cpp`
- plus the existing local IR controller changes already present in the working tree at the time this baseline was frozen

How to return to this exact baseline later:
1. Use the folder `C:\GitHub\lilygo\My-TTGO-Watch-Gen1`.
2. Do not copy changes back into it.
3. Rebuild from this folder or flash the preserved binaries from `.pio\build\t-watch2020-v3-s3`.

Related working folder for future edits:
- `C:\GitHub\lilygo\My-TTGO-Watch-Gen2`
