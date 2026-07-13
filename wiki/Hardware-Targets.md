# Hardware Targets

XNODE currently builds four supported firmware targets from this repo.

## T-Watch Ultra

PlatformIO environment:

```powershell
pio run -e t-watch-ultra
```

Role:

- Primary watch build for the larger Ultra screen.
- Tactical map with larger markers and a large top-rendered GPS position triangle.
- GPS status page and automatic map GPS startup.
- Meshtastic radio service.
- BLE bridge for XTOC/XCOM.
- Power-managed idle behavior.
- DRV2605 haptic feedback through the Ultra haptic rail and XL9555 enable line.

Important build traits:

- Board file: `boards/twatch_ultra.json`
- Partition file: `default_16MB.csv`
- DIO flash mode.
- Post-upload watchdog reset script: `support/twatch_ultra_post_upload_reset.py`

## T-Watch S3 / Gen3

PlatformIO environment:

```powershell
pio run -e t-watch2020-v3-s3
```

Role:

- Existing watch variant that shares the XNODE app flows.
- Protected by the same regression checks so Ultra/T-Deck changes do not silently break it.

Important build traits:

- Board file: `boards/twatch2020_s3.json`
- Partition file: `default_16MB.csv`
- In-repo support libraries under `support/twatch-s3-libdeps`.

## T-Deck Plus

PlatformIO environment:

```powershell
pio run -e tdeck-plus
```

Role:

- Larger 320x240 screen XNODE device.
- Built-in hardware keyboard for mesh/message entry.
- Same core launcher, messages, mesh, tactical map, GPS status, Alert Summary, SOS, and CheckIn workflows.
- Wide tactical map view showing synced XTOC/XCOM markers on the larger display.
- Produces a firmware binary suitable for manual install or bmorcelli Launcher testing.

Firmware output:

```text
.pio/build/tdeck-plus/firmware.bin
```

Important hardware mappings:

| Function | Mapping |
| --- | --- |
| Display | 320x240 ST7789 |
| TFT CS/DC/backlight | GPIO `12/11/42` |
| Touch | GT911 on I2C GPIO `18/8`, interrupt GPIO `16` |
| Keyboard | I2C controller at `0x55`, interrupt GPIO `46` |
| LoRa | SX1262 on shared SPI GPIO `41/38/40`, CS `9`, BUSY `13`, RST `17`, DIO1 `45` |
| GPS | `Serial1`, RX `44`, TX `43` |
| SD card | CS `39` on shared SPI |
| Battery estimate | ADC GPIO `4` |

Important T-Deck behavior:

- Upload speed is `460800` for reliability.
- DIO flash mode is used to avoid boot loops seen with QIO.
- Post-upload reset is handled by `support/tdeck_plus_post_upload_reset.py`.
- The display timeout blanks backlight without entering the full watch standby path.
- Touch and hardware keyboard remain activity/wake sources.
- The target currently does not implement trackball, speaker, microphone, IMU/compass, pedometer, or RTC alarm integration.
- The current T-Deck Plus hardware target does not expose an onboard vibration motor, so XNODE treats haptic feedback as unavailable on this target.

## T-Deck Pro

PlatformIO environment:

```powershell
pio run -e tdeck-pro
```

Role:

- Portrait 240x320 e-paper XNODE handheld target.
- Built-in hardware keyboard for message entry.
- Touch and swipe navigation on the Pro e-paper UI.
- Same core launcher, messages, mesh, tactical map, GPS status, Alert Summary, SOS, and CheckIn workflows.
- Produces a firmware binary suitable for manual install or bmorcelli Launcher testing.

Firmware output:

```text
.pio/build/tdeck-pro/firmware.bin
```

Important hardware mappings:

| Function | Mapping |
| --- | --- |
| Display | 240x320 GDEQ031T10 e-paper through GxEPD2 |
| Touch | HYN/CST touch path on I2C, Pro V2 reset/int pins |
| Keyboard | TCA8418 I2C keyboard controller |
| Haptics | DRV2605 at I2C `0x5A` |
| Battery gauge | BQ27220 at I2C `0x55` |
| LoRa | SX1262 on shared SPI |
| GPS | `Serial1`, RX `44`, TX `43` |
| SD card | Shared SPI bus |

Important T-Deck Pro behavior:

- Upload speed is `460800` for reliability.
- Pro hardware support libraries are vendored under `support/tdeck-pro-libdeps`.
- Full-height LVGL draw buffers and an explicit startup refresh are used so the whole e-paper main page paints at boot.
- HYN touch is polled directly for UI input; swipe navigation has both LVGL gesture and raw-sample fallback paths.
- Status bar and main page styling are adjusted for black-on-white e-paper contrast.
- E-paper refresh can briefly invert during full refreshes and is slower than LCD/AMOLED targets.
