# Build, Flash, Test, and CI

This repo is set up so all supported firmware targets can be built from the same tree.

## Install prerequisites

The project uses:

- Node/npm for test scripts.
- Python for regression scripts.
- PlatformIO for firmware builds.
- Arduino ESP32 / ESP32-S3 PlatformIO environments.

No new dependency should be added casually. The firmware already declares its PlatformIO libraries in `platformio.ini`, and several S3 support libraries live under `support/twatch-s3-libdeps`.

## Test

Run:

```powershell
npm test
```

This runs:

```text
python support/check_watch_overlay_persistence.py
python support/regression_checks.py
```

The regression checks protect the recent high-risk behavior:

- Touch responsiveness on small screens.
- Message shortcut clearing.
- Watch keyboard paging/space behavior.
- Tactical map GPS marker.
- Tactical map controls and performance mode.
- Map pan behavior over markers.
- T-Deck Plus viewport behavior.
- T-Deck Pro e-paper first paint, touch, and swipe behavior.
- GPS diagnostics layout.
- GPS pin/probe/time-sync behavior.
- Power defaults.
- Pedometer continuity on supported watch hardware.
- T-Deck theme/display/wake behavior.
- CI target coverage.

## Build all supported targets

Run:

```powershell
npm run build
```

This runs tests and builds:

```text
t-watch-ultra
t-watch2020-v3-s3
tdeck-plus
tdeck-pro
```

## Build one target

```powershell
pio run -e t-watch-ultra
pio run -e t-watch2020-v3-s3
pio run -e tdeck-plus
pio run -e tdeck-pro
```

## Flash

Ultra example:

```powershell
pio run -e t-watch-ultra -t upload --upload-port COM10
```

T-Deck Plus example:

```powershell
pio run -e tdeck-plus -t upload --upload-port COM20
```

T-Deck Pro example:

```powershell
pio run -e tdeck-pro -t upload --upload-port COM7
```

Use the actual COM port Windows assigned to the attached device.

## Firmware artifacts

T-Deck Plus binary for manual or Launcher testing:

```text
.pio/build/tdeck-plus/firmware.bin
```

T-Deck Pro binary for manual or Launcher testing:

```text
.pio/build/tdeck-pro/firmware.bin
```

Watch binaries are emitted under their corresponding `.pio/build/<env>/` folders.

## GitHub Actions

CI is defined in:

```text
.github/workflows/ci.yml
```

It runs on push, pull request, and manual workflow dispatch. The current workflow installs PlatformIO and runs:

```powershell
npm run build
```

That means every CI run should cover regression tests and all four active firmware targets.

## Post-upload reset behavior

Ultra and T-Deck Plus use post-upload reset helpers so devices do not remain trapped in the flasher stub after programming. T-Deck Pro currently uses `board_upload.after_reset = no_reset_stub`; if a tool leaves it in the stub, issue an ESP32-S3 run/reset command or power cycle the board.

- `support/twatch_ultra_post_upload_reset.py`
- `support/tdeck_plus_post_upload_reset.py`

If a device still needs manual power cycling after upload, check the upload port, baud rate, USB cable, and whether the correct target environment was used.
