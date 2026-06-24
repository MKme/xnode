# Mesh, Messaging, and Bridge

XNODE uses the radio and BLE paths to fit into the wider XTOC/XCOM transport model.

## Meshtastic service

On supported hardware, the firmware initializes the SX1262 radio with RadioLib and runs a Meshtastic-compatible service for text and node information workflows.

Implemented service capabilities include:

- Radio initialization and receive loop.
- Text send and receive.
- Channel selection.
- Channel info get/set.
- User long name and short name get/set.
- Node info broadcast scheduling.
- Last peer/RSSI/SNR/message tracking.
- Power-management callbacks for standby behavior.

The T-Deck Plus target is treated as a Meshtastic `T_DECK` hardware model in the onboard radio and BLE user config paths.

## Mesh UI

The mesh UI supports:

- Compose and send.
- Hardware keyboard entry on T-Deck Plus.
- Multi-bank on-screen keyboard behavior on watch devices.
- Channel and destination behavior from the Meshtastic service.
- Exit/close controls sized and placed for reliable touch on small screens.

## BLE XNODE bridge

XTOC and XCOM use the XNODE BLE bridge for device sync. Current bridge capability names include:

- `sync`
- `location`
- `meshtastic`
- `basemap`
- `mapOverlay`
- `newsNotifications`
- `ble`

The bridge is how the host tools move watch-visible state without treating the watch as a generic text terminal.

## Alerts and messages

Host-pushed alert/news items are stored in the Alert Summary app and can be shown on the watch. The main clock message shortcut is treated only as a new-message indicator. Opening the message view clears that indicator while preserving stored messages and the normal Messages launcher entry.

## SOS and CheckIn packet paths

SOS:

- Sends a clear XTOC `SITREP` packet.
- Uses Unit ID, destination Unit ID, priority/status fields, current lat/lon, and `Manual SOS` note.

CheckIn:

- Sends a clear XTOC `CHECKIN/LOC` packet.
- Uses Unit ID, OK status, current lat/lon, and timestamp.

Both flows depend on GPS/location validity and a ready radio/channel.

## How this fits XTOC/XCOM

XTOC and XCOM are designed to move compact operational packets across many transports. XNODE adds a field device that can:

- Display what the TOC pushed.
- Send minimal high-value packets back.
- Track and show its own location.
- Carry mesh traffic without a phone/tablet UI being open all the time.

