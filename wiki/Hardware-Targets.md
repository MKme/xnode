# Hardware Targets

XNODE currently builds three supported firmware targets from this repo.

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
- The target currently does not implement trackball, speaker, microphone, vibration, IMU/compass, pedometer, or RTC alarm integration.

