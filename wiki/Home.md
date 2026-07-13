# XNODE Wiki

XNODE is the MKME tactical watch and handheld firmware for the X software stack. It runs on LilyGO watch-class ESP32-S3 hardware plus the LilyGO T-Deck Plus and T-Deck Pro, then acts as the wrist or handheld edge node for XTOC and XCOM.

At the system level:

- XTOC is the offline command center and tactical operations layer.
- XCOM is the offline field radio and mapping companion.
- XNODE is the wearable or handheld edge device that shows alerts, map overlays, GPS position, mesh traffic, SOS, and check-in flows.
- XINTEL is the local radio intelligence monitor that turns legally receivable audio into structured intel for XTOC.
- XCORE is the local AI analyst that can reason over XTOC and XCOM operational data without depending on cloud AI.

This directory is the source for the repository wiki. The Markdown files are written in GitHub Wiki style so they can be published directly to the GitHub wiki when the special `MKme/xnode.wiki.git` repo is initialized.

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

## Primary source references

- XNODE GitHub repo: https://github.com/MKme/xnode
- MKME Learn XNODE/XTOC update: https://learn.mkme.org/2026/05/03/wrist-controlled-war-room-xtoc-update-xnode-live-isr-mesh/
- XTOC Store page: https://store.mkme.org/product/xtoc-tactical-operations-center-software-suite/
- XCOM Store page: https://store.mkme.org/product/xcom-offline-radio-communication-suite/
- XCORE Store page: https://store.mkme.org/product/xcore/
- XINTEL Store page: https://store.mkme.org/product/xintel/
