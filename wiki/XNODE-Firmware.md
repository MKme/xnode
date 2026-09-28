# XNODE Firmware

The XNODE firmware turns LilyGO watch-class hardware plus the T-Deck Plus and T-Deck Pro into compact tactical endpoints for the MKME X stack.

## Field Chronometer home

![XNODE Field Chronometer home for T-Deck Plus](https://raw.githubusercontent.com/MKme/xnode/main/site/images/home-tactical-2026-09-28/tdeck-plus-device.png)

The tactical home keeps the clock, full date, battery and charging state, wireless indicators and Messages shortcut together. Moon phase and illumination are available on supported boards, with temperature and humidity when a configured sensor supplies them. The Mission Teal dark background continues through apps, settings and the status bar; color displays also retain the light theme; T-Deck Pro uses monochrome. The screenshot is captured from a physical T-Deck Plus display.

## Navigation and tactical menus

Swipe **left or right** through **Home > Apps > Setup > Notes**. Apps and Setup may have several occupied pages; their footers show the current page. Swipe back to return. Inside a tool, use left/right swipes for its pages and its existing Back or Exit control to leave. Lists and text still scroll normally, and maps retain panning. Tap the top status bar to open or close quick settings.

Tap an icon card or its caption to open the tool. Tactical icon colors distinguish communications, navigation, power, alerts and system tools; they do not indicate connection or success. The T-Deck Pro keeps monochrome icons.

| Apps | Setup |
| --- | --- |
| ![T-Deck Plus tactical Apps menu](https://raw.githubusercontent.com/MKme/xnode/main/site/images/home-tactical-2026-09-28/tdeck-plus-dark-apps.png) | ![T-Deck Plus tactical Setup menu](https://raw.githubusercontent.com/MKme/xnode/main/site/images/home-tactical-2026-09-28/tdeck-plus-dark-setup.png) |

Menu images are firmware renderings with a representative selection of tools. The status bar is outside these menu captures; the available pages follow the tools included on the device.

## Main app surfaces

Current user-facing surfaces include:

- Field Chronometer home with clock, date, device status and board-supported moon information.
- Launcher.
- Messages.
- Mesh Chat with channel conversations, an 80-character composer and a neighboring Radio page.
- Tactical map.
- GPS settings/status diagnostics.
- Alert Summary.
- SOS.
- CheckIn.
- Display, GPS, BLE, WiFi, touch, battery, and other setup pages.

## Mesh Chat

Open **mesh** from **APPS** for the selected channel's conversation. Swipe left/right between **CHAT** and **RADIO**; scroll messages vertically. T-Deck Plus uses its physical keyboard. Watches use a full-screen on-screen editor with large key pages; T-Deck Pro uses the portrait editor. Production opens directly into live traffic with no Examples button. **SEND** broadcasts to the selected channel even after a **DIRECT** incoming message.

The latest 24 real text records across channels remain in RAM until restart. **TX SENT (LOCAL)** describes the local radio, not recipient delivery. See [Mesh, Messaging, and Bridge](Mesh-Messaging-and-Bridge) for the complete procedure and status meanings.

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

## T-Deck Pro e-paper behavior

The T-Deck Pro target uses a 240x320 portrait e-paper display. Startup forces a full LVGL render so the whole main page appears, then the Pro framebuffer path handles e-paper refresh separately from the T-Deck Plus LCD path. Touch is read through the local HYN/CST path. The Pro uses the same horizontal menu order as the color-screen models, with its e-paper touch handling retained.

## Power behavior

The firmware distinguishes idle power savings from active-use responsiveness:

- Active map/watchface paths use performance mode where needed.
- Standby/hibernate paths release performance mode.
- Ultra starts GPS/WiFi idle paths conservatively while keeping BLE discoverable for XTOC/XCOM sync.
- T-Deck display timeout blanks backlight while keeping LVGL/touch/keyboard active to avoid the previous wake stutter state.
- T-Deck Pro keeps the e-paper UI active and refreshes deliberately; deeper Pro-specific power policy still needs field validation.

See [Operations and Troubleshooting](Operations-and-Troubleshooting) for field checks.

On watches, the conversation, **WRITE**, and **RADIO** occupy separate horizontal pages. Tap **WRITE** to open the draft, tap its text box for the keyboard, confirm to return to the draft review, then tap the full-width **SEND** button. An accepted send returns to the conversation; rejection keeps the draft and shows the error. Choose the channel on **RADIO**. The arrow on WRITE returns to the conversation without discarding the draft.
