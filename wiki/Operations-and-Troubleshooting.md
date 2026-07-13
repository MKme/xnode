# Operations and Troubleshooting

This page captures the common checks that matter during field use and firmware bring-up.

## GPS no fix

Open GPS status before debugging the map.

Interpretation:

- `Raw RX: 0`: receiver is not sending bytes. Check power rail, pins, UART, board target, receiver baud, and hardware connection.
- Raw bytes increment but NMEA bad count rises: wrong baud, bad signal, noise, or parse issue.
- NMEA OK count rises but fix is no: receiver works but has not locked satellites yet.
- Valid fix but no map marker: check map app, marker visibility, basemap projection metadata, and local position update path.

On T-Deck Plus and T-Deck Pro, GPS should use `Serial1` RX `44` and TX `43`.

## Clock not syncing

GPS time sync requires valid GPS UTC date/time. A receiver can output NMEA without a valid fix/time early in acquisition. If GPS status shows no valid UTC, the clock should not be expected to update yet.

If Ultra syncs and T-Deck does not, check:

- T-Deck GPS status raw/NMEA/fix/time fields.
- Receiver probe result and baud.
- Whether the build target was `tdeck-plus`.
- Whether the build target was `tdeck-pro` for a Pro unit.
- Whether the board was flashed with the current repo output.

## Map position missing

The map needs:

- GPS powered and producing valid fix.
- Local position update path active.
- Installed basemap metadata that matches the map area.
- Local marker object visible and above overlays.

If only overlay markers are visible, GPS probably has no usable fix or the local marker projection is outside the current basemap.

## Map pan reverses or jumps near markers

The current fix makes marker objects and old invisible nav pads non-clickable and routes drag movement through raw touch deltas. If this regresses, check `src/app/osmmap/osmmap_app_main.cpp` for:

- `osmmap_handle_touch_pan`
- `osmmap_touch_pan_consuming`
- `lv_obj_set_click( item->marker_obj, false )`
- `lv_obj_set_click( item->marker_label_obj, false )`
- `lv_obj_set_click( osmmap_north_btn, false )`
- `lv_obj_set_click( osmmap_zoom_southeast_btn, false )`

`support/regression_checks.py` should also contain the corresponding guard.

## Touch buttons require multiple presses

Small screens need large press targets and press-event handling. If a page becomes hard to exit:

- Check for tiny `x` buttons near the bezel.
- Prefer the shared large close-button pattern used by GPS status, tac map, SOS, CheckIn, alerts, and mesh.
- Ensure duplicate click suppression is not blocking the first press.
- Run `npm test` and verify the button regression checks pass.

## Vibration missing

Target behavior:

- T-Watch Ultra uses the DRV2605 haptic driver at I2C `0x5A`. The firmware enables the Ultra haptic rail and XL9555 enable line only while a click waveform is being played.
- T-Watch S3 uses the existing LilyGO `SensorDRV2605` path.
- T-Deck Plus does not expose an onboard vibration motor in the current hardware target, so haptic feedback is intentionally unavailable on that target.

If Ultra vibration fails, check:

- `src/hardware/twatch_ultra_hal.*` for `beginHaptic()` and `vibrate()`.
- `src/hardware/motor.cpp` for the Ultra branch calling `watch.vibrate()`.
- The haptic rail behavior through `WATCH_POWER_DRV2605`.
- I2C access to DRV2605 address `0x5A`.

## T-Deck display sleeps and wakes slowly

T-Deck Plus intentionally blanks the backlight instead of entering the full watch standby path. This keeps LVGL, touch, and keyboard active and avoids the slow-motion/stutter state seen when the UI stack was partially suspended.

If wake becomes slow again, check:

- `src/hardware/display.cpp`
- `src/hardware/tdeck_plus_hal.*`
- T-Deck standby regression checks.

## T-Deck Pro e-paper display only shows status icons

The Pro target needs the full-height e-paper framebuffer and startup redraw path. If the main page regresses to only status icons:

- Verify the build target was `tdeck-pro`, not `tdeck-plus`.
- Check `src/hardware/framebuffer.h` for the Pro `FRAMEBUFFER_BUFFER_H RES_Y_MAX` setting.
- Check `src/gui/gui.cpp` for the Pro startup `lv_refr_now(NULL)` plus `framebuffer_refresh()` path.
- Check `src/hardware/tdeck_pro_hal.cpp` for the GxEPD2 full-window refresh path.

## T-Deck Pro touch or swipe does not navigate

The Pro status bar can receive taps even when the mainbar swipe path is broken. If top-bar taps work but center-screen swipes do not:

- Check that HYN touch initializes in the boot log: `[TDECKPRO] touch: OK (HYN)`.
- Check `src/hardware/touch.cpp` for Pro HYN polling and `touch_get_swipe_delta`.
- Check `src/gui/mainbar/mainbar.cpp` for `mainbar_poll_tdeck_pro_gesture`.
- Swipe from the center of the screen, not from the top status bar.

## Battery drain at idle

Ultra idle behavior should keep unnecessary radios off:

- GPS off at clean idle unless an app starts it.
- WiFi off by default.
- BLE advertising on by default so XTOC/XCOM can discover the XNODE bridge.
- LoRa chip sleeping in standby.
- Display sleeping after timeout.
- No background GPS loop pinning performance mode.

Active app behavior is different: tac map and normal awake watchface can use performance mode so the watch remains responsive while being used.

## Flash/upload loops

If Windows chimes repeatedly or the device appears stuck after flashing:

- Verify the correct PlatformIO environment.
- Use the reliable upload speed for the target.
- Confirm the post-upload reset helper ran.
- Try a different cable/port if the serial link drops mid-upload.
- For T-Deck Plus, keep DIO flash mode; QIO caused boot loops on the attached unit.

## Field operating notes

- Cache or install maps before offline use.
- Verify GPS outdoors before relying on map position.
- Verify Unit ID, channel, and destination before relying on SOS or CheckIn.
- Test mesh send/receive before leaving the staging area.
- Keep XTOC/XCOM as the operational record; XNODE is the fast endpoint and display.
