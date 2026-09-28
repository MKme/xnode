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
- The latest 24 text records across channels, held in RAM until restart.
- Power-management callbacks for standby behavior.

The T-Deck Plus target is treated as a Meshtastic `T_DECK` hardware model in the onboard radio and BLE user config paths.

## Mesh Chat

Open **mesh** from **APPS**, or select **MESH** on a home layout that offers it. **CHAT** opens the selected channel's conversation. Scroll messages vertically; swipe left/right or tap the footer to move between **CHAT** and **RADIO**. Use **NEW v** to return to the latest activity when reading earlier messages. The top-left arrow leaves the app.

Mesh Chat opens directly into live traffic. Choose a channel and use **SEND** to transmit. Production firmware has no Examples button and ignores any example-mode preference saved by earlier firmware.

On watches, the conversation, **WRITE**, and **RADIO** occupy separate horizontal pages. Tap **WRITE** to open the draft, tap its text box for the keyboard, confirm to return to the draft review, then tap the full-width **SEND** button. An accepted send returns to the conversation; rejection keeps the draft and shows the error. Choose the channel on **RADIO**. The arrow on WRITE returns to the conversation without discarding the draft.

Choose a channel, tap the composer and enter up to **80 characters**. T-Deck Plus uses its physical keyboard. Watches use a full-screen editor: the draft stays above large keys, **A-M**, **N-Z** and **123** select key pages, and the checkmark returns the text to chat. T-Deck Pro uses the portrait on-screen editor. X closes the on-screen editor without applying its changes. Back in chat, review the channel and tap **SEND**.

**SEND broadcasts to the selected channel**, including after a received message marked **DIRECT**. There is no automatic private-reply mode. An immediate send rejection keeps the draft. **QUEUED** means an accepted attempt is pending; **TX SENT (LOCAL)** means the local radio reported completion, not that the recipient received it. **TX UNCONFIRMED** means transmission failed or completion was not observed. Check **RADIO** and the receiver before retrying: an uncertain completion does not prove that no RF transmission occurred.

**RADIO** shows local identity, channel/frequency, radio state and the last observed peer signal. Mesh Chat keeps the latest **24 real text records across all channels** and displays the selected channel's subset. New records replace the oldest. History and drafts clear on restart; this chat does not save message bodies to flash. Message times use the local clock, or **--:--** if the clock is unset. Host-pushed **Messages** remain a separate view.

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
Firmware advertises the XNODE BLE service UUID alongside the Meshtastic BLE service so browser-based XTOC/XCOM sync can discover and open the bridge directly.

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
