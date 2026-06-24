# Product Ecosystem

The MKME X stack is a local-first operational toolkit. XTOC, XCOM, XNODE, XINTEL, and XCORE each solve a different part of off-grid coordination, but they are designed to exchange the same operational picture: roster, packets, map overlays, mesh traffic, alerts, GPS positions, radio-derived intel, and analysis results.

## XTOC

XTOC is the Tactical Operations Center software. The Store page describes it as an offline browser-based command center that turns structured operational reports into compact packets that can be carried over QR, copy/paste, email, mesh, Winlink-style paths, or radio relays.

In the XNODE workflow, XTOC is the main command-side source of:

- Roster and Unit IDs.
- SITREP, TASK, CHECKIN/LOC, CONTACT, RESOURCE, ASSET, ZONE, MISSION, EVENT, route, and other packet records.
- Tactical map basemaps and marker overlays.
- Alerts/news items that become watch-visible notifications.
- Shared Workspace/TOC Node context for larger multi-station operations.

XTOC also integrates with SATCOM/TLE mapping, ATAK/KML/CoT workflows, ADS-B aircraft overlays, mesh transports, MANET/LAN bridges, Reticulum/RNode MeshChat, APRS/JS8Call/FLDIGI/VarAC bridges, ViperGram radio-audio transport, XINTEL, and XCORE.

## XCOM

XCOM is the field-side offline radio and mapping suite. The Store page describes it as an installable PWA for iOS, Android, and desktop that keeps key field tools available with no signal after it has been installed and cached.

XCOM is the mobile/operator-side companion for:

- Repeater, packet-station, shortwave, callsign, HF prediction, SATCOM, and map references.
- XTOC packet creation, storage, import, export, QR, copy/paste, radio-audio, mesh, APRS, MANET, and Reticulum workflows.
- Tactical map overlays from XTOC and live node overlays from Meshtastic, MeshCore, and OpenMANET.
- BLE bridge sync with XNODE for watch-visible map, alert, location, and packet data.

In practice, XCOM is what a field operator uses when they need a phone/tablet/laptop interface, while XNODE is the wearable or compact device that stays on the operator.

## XNODE

XNODE is the firmware in this repository. The GitHub repo description identifies it as the tactical watch with LoRa radio and ESP32 core for use with XTOC and XCOM.

The Learn update frames XNODE as the wrist-control layer for a larger XTOC system: a watch running Meshtastic, live ISR camera feeds, mesh networking, and real-time data flow into XTOC. This repo implements the watch/T-Deck firmware side of that concept.

XNODE handles:

- Main watch/launcher UI.
- Messages, mesh compose/receive, SOS, CheckIn, and Alert Summary screens.
- GPS status and GPS time/location integration.
- Tactical map rendering from an installed basemap.
- Local user-location marker and host-pushed overlay markers.
- BLE bridge capabilities consumed by XTOC and XCOM.
- Meshtastic-compatible radio transport on supported hardware.
- Power management, display sleep/wake, and hardware-specific HAL paths.

## XINTEL

XINTEL is the radio intelligence monitor. The Store page describes it as a Windows app that ingests legally receivable audio, transcribes locally, scans for operator-defined rules, and pushes structured intel hits into XTOC.

XINTEL complements XNODE because it turns passive radio monitoring into TOC-visible structured data. A field team can then see resulting events or alerts through XTOC/XCOM and, where bridged, on XNODE screens.

XINTEL also decodes ViperGram audio bursts produced by XTOC/XCOM and can forward recovered packet text into XTOC.

## XCORE

XCORE is the local tactical AI analyst. The Store page describes it as a Windows local-first analyst that uses CPU GGUF models and operational data such as AO, roster, packets, SITREPs, and cached ADS-B aircraft to generate summaries, anomaly scans, 24-hour SITREPs, aircraft pattern checks, and packet drafts.

XCORE complements XNODE indirectly:

- XNODE sends or displays field observations and mesh traffic.
- XTOC/XCOM collect and structure those observations.
- XCORE analyzes the local operational data and can help produce new reports or operator guidance.
- XTOC/XCOM can then send actionable outputs back toward field devices and the watch.

## Why the stack works together

The design is not centered on one perfect network. The common unit is structured operational data that can survive bad links. XTOC and XCOM can move packets through many transports. XNODE keeps the most important field functions on a small device. XINTEL turns radio/audio monitoring into structured events. XCORE adds offline analysis. Together, they let the operator move from raw observations to a shared operational picture without requiring constant internet access.

## Sources

- XNODE GitHub repo: https://github.com/MKme/xnode
- MKME Learn XNODE/XTOC update: https://learn.mkme.org/2026/05/03/wrist-controlled-war-room-xtoc-update-xnode-live-isr-mesh/
- XTOC Store page: https://store.mkme.org/product/xtoc-tactical-operations-center-software-suite/
- XCOM Store page: https://store.mkme.org/product/xcom-offline-radio-communication-suite/
- XCORE Store page: https://store.mkme.org/product/xcore/
- XINTEL Store page: https://store.mkme.org/product/xintel/

