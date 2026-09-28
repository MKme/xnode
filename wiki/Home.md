# XNODE Wiki

XNODE is the MKME tactical watch and handheld firmware for the X software stack. It runs on LilyGO watch-class ESP32-S3 hardware plus the LilyGO T-Deck Plus and T-Deck Pro, then acts as the wrist or handheld edge node for XTOC and XCOM.

At the system level:

- XTOC is the offline command center and tactical operations layer.
- XCOM is the offline field radio and mapping companion.
- XNODE is the wearable or handheld edge device that shows alerts, map overlays, GPS position, mesh traffic, SOS, and check-in flows.
- XINTEL is the local radio intelligence monitor that turns legally receivable audio into structured intel for XTOC.
- XCORE is the local AI analyst that can reason over XTOC and XCOM operational data without depending on cloud AI.

This directory is the source for the repository wiki. The Markdown files are written in GitHub Wiki style so they can be published directly to the GitHub wiki when the special `MKme/xnode.wiki.git` repo is initialized.

## Field Chronometer home

![XNODE Field Chronometer home for T-Deck Plus](https://raw.githubusercontent.com/MKme/xnode/main/site/images/home-tactical-2026-09-28/tdeck-plus-device.png)

The tactical home keeps the clock, full date, battery and charging state, wireless indicators and Messages shortcut together. Moon phase and illumination are available on supported boards, with temperature and humidity when a configured sensor supplies them. The Mission Teal dark background continues through apps, settings and the status bar; color displays also retain the light theme; T-Deck Pro uses monochrome. The screenshot is captured from a physical T-Deck Plus display.

## Navigation and tactical menus

Swipe **left or right** through **Home > Apps > Setup > Notes**. Apps and Setup may have several occupied pages; their footers show the current page. Swipe back to return. Inside a tool, use left/right swipes for its pages and its existing Back or Exit control to leave. Lists and text still scroll normally, and maps retain panning. Tap the top status bar to open or close quick settings.

Tap an icon card or its caption to open the tool. Tactical icon colors distinguish communications, navigation, power, alerts and system tools; they do not indicate connection or success. The T-Deck Pro keeps monochrome icons.

## Mesh Chat

Open **mesh** from **APPS** to read a channel conversation. **CHAT** and **RADIO** are horizontal pages. The 80-character composer uses the T-Deck Plus keyboard or a full-screen on-screen editor on watches and T-Deck Pro. Production opens directly into live traffic. **SEND** broadcasts to the selected channel. Read [Mesh, Messaging, and Bridge](Mesh-Messaging-and-Bridge) for history limits and local transmission status.

## Pages

- [Product Ecosystem](Product-Ecosystem): what XTOC, XCOM, XNODE, XINTEL, and XCORE do.
- [System Architecture](System-Architecture): how data moves between the products.
- [XNODE Firmware](XNODE-Firmware): firmware features and app responsibilities.
- [Hardware Targets](Hardware-Targets): supported boards and hardware mappings.
- [Tactical Map, GPS, and Overlays](Tactical-Map-GPS-and-Overlays): map workflow, position marker, overlays, and GPS behavior.
- [Mesh, Messaging, and Bridge](Mesh-Messaging-and-Bridge): Meshtastic, BLE bridge, alerts, SOS, and check-in.
- [Build, Flash, Test, and CI](Build-Flash-Test-and-CI): repeatable build and regression workflow.
- [Operations and Troubleshooting](Operations-and-Troubleshooting): field checks and known failure modes.
- [Publishing the GitHub Wiki](Publishing-the-GitHub-Wiki): how to publish these files to GitHub Wiki.

## Current firmware targets

| Target | PlatformIO environment | Role |
| --- | --- | --- |
| LilyGO T-Watch Ultra | `t-watch-ultra` | Current watch-first XNODE build with GPS, map overlays, Meshtastic, power management, and large tactical markers. |
| LilyGO T-Watch S3 / Gen3 | `t-watch2020-v3-s3` | Existing watch variant protected by shared regression checks. |
| LilyGO T-Deck Plus | `tdeck-plus` | Larger-screen XNODE target with hardware keyboard, GPS diagnostics, wide tactical map, synced XTOC/XCOM markers, mesh, SOS, CheckIn, and Launcher-ready firmware output. |
| LilyGO T-Deck Pro | `tdeck-pro` | Portrait 240x320 e-paper XNODE target with local Pro HAL, HYN touch, swipe navigation, keyboard input, haptics, and Launcher-ready firmware output. |

## T-Deck Plus map proof

![T-Deck Plus XNODE tactical map showing synced XTOC and XCOM markers on the larger screen](https://raw.githubusercontent.com/MKme/xnode/main/site/images/T-dec/IMG_6817.jpg)

The T-Deck Plus tactical map now shows the synced XTOC/XCOM tactical markers on the larger display. This gives the same XNODE map workflow more usable screen area than the watch-only layout while preserving the shared overlay and basemap sync path.

## T-Deck Pro screens

| Clock and status | Main launcher | Meshtastic messaging |
| --- | --- | --- |
| ![T-Deck Pro Field Chronometer firmware rendering with illustrative readings](https://raw.githubusercontent.com/MKme/xnode/main/site/images/home-tactical-2026-09-28/tdeck-pro-light.png) | ![T-Deck Pro monochrome XNODE Apps menu rendering](https://raw.githubusercontent.com/MKme/xnode/main/site/images/home-tactical-2026-09-28/tdeck-pro-light-apps.png) | ![T-Deck Pro Mesh Chat firmware capture with example conversation](https://raw.githubusercontent.com/MKme/xnode/main/site/images/mesh-chat-2026-09-28/tdeck-pro-light-chat.png) |

The home image shows the monochrome firmware layout with illustrative readings. The Apps menu is a firmware rendering with selected tools; the Mesh Chat firmware capture shows the portrait conversation layout with illustrative example messages.

## Primary source references

- XNODE GitHub repo: https://github.com/MKme/xnode
- MKME Learn XNODE/XTOC update: https://learn.mkme.org/2026/05/03/wrist-controlled-war-room-xtoc-update-xnode-live-isr-mesh/
- XTOC Store page: https://store.mkme.org/product/xtoc-tactical-operations-center-software-suite/
- XCOM Store page: https://store.mkme.org/product/xcom-offline-radio-communication-suite/
- XCORE Store page: https://store.mkme.org/product/xcore/
- XINTEL Store page: https://store.mkme.org/product/xintel/

On watches, the conversation, **WRITE**, and **RADIO** occupy separate horizontal pages. Tap **WRITE** to open the draft, tap its text box for the keyboard, confirm to return to the draft review, then tap the full-width **SEND** button. An accepted send returns to the conversation; rejection keeps the draft and shows the error. Choose the channel on **RADIO**. The arrow on WRITE returns to the conversation without discarding the draft.
