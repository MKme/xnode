# Meshtastic / MeshCore in one XNODE image

This change adds a boot-selectable mesh protocol to the four active XNODE targets:
`t-watch2020-v3-s3`, `t-watch-ultra`, `tdeck-plus`, and `tdeck-pro`.
Legacy PlatformIO environments are outside this change's support scope.

Both protocols are compiled into the same board-specific firmware. Switching
protocols requires a **restart, not another flash**. Only the selected stack owns
the SX1262 during a boot. This is not a gateway between protocols, and it does not
receive both networks simultaneously.

## Change the protocol on the device

1. Open **mesh**, then its **RADIO** page.
2. Tap **Switch to MeshCore** (or **Switch to Meshtastic**) and confirm **Save**.
3. Review **Active** and **Selected**. The current network remains active until
   restart; selecting a mode does not interrupt a packet or reinitialize SX1262.
4. Tap **Restart** and confirm. Unsaved drafts and the RAM-only chat history are
   cleared. Pending radio traffic may be lost, so finish important sends first.
5. Return to RADIO and verify the active protocol, channel, radio profile, and
   status before sending. Other nodes must use the same protocol and profile.

You can cancel the saved switch before restarting by selecting the current mode
again. A failed preference write does not change the selected or active mode.
Invalid or unsupported saved protocol values fall back to Meshtastic.

## What the MeshCore implementation supports

- MeshCore's upstream packet framing, channel encryption and group chat, using a
  pinned core and BaseChatMesh rather than a new wire-protocol approximation
- Eight independently configured channel slots, with sparse enabled-channel
  selection and strict 16-byte or 32-byte PSK validation
- Independent persistent MeshCore identity, user names, channels, and modem
  configuration; Meshtastic configuration is retained when switching back
- A complete 32-byte public key, shown as 64 hexadecimal digits and exposed over
  the XNODE bridge; a truncated key is never used as a radio destination
- Bounded 24-message RAM history, local TX state, and received chat notifications
- Signed node advertisements and received advertisement positions on the existing map; self advertisements currently contain a name without location
- XNODE group-text transport for SOS and CheckIn through the selected protocol
- XNODE's existing custom BLE bridge with explicit protocol information

Group messages have **no recipient-delivery acknowledgment**. A local TX_DONE
means only that this device transmitted. A receive echo or matching text is not
reported as a delivery ACK. MeshCore group sender names are display text, not a
verified individual identity.

The existing XNODE composer broadcasts to the selected channel. Direct-message, path and control traffic is outside this group-only build.
Outbound direct messages using Meshtastic's 32-bit destination API are rejected
in MeshCore mode.
This build does not provide the official MeshCore companion-app BLE protocol,
repeater or room-server management, or a MeshCore/Meshtastic radio bridge.
Companion clients must implement the new XNODE commands/events below to control
MeshCore. XTOC and XCOM expose these commands in their XNODE radio controls; see XNODE-CLIENT-RADIO.md.
The official Meshtastic protobuf BLE service is only exposed in Meshtastic mode;
it must not configure MeshCore by interpreting Meshtastic-format fields.

## Radio profile and hardware scope

The MeshCore default is **910.525 MHz, 62.5 kHz bandwidth, SF7, CR4/5** in the
existing firmware's 902–928 MHz regional band. It is not a universal MeshCore
preset and will not communicate with a network using different settings. Match
all modem parameters to your local network and permitted operating conditions.
The configuration validator intentionally rejects frequencies outside this band,
including occupied bandwidth extending past either edge. Different regional
hardware and regulations are not covered by this change.

The active SX1262 adapters preserve board-specific TCXO settings: 3.0 V on
T-Watch S3, 1.8 V on Ultra and both T-Decks. The repository's RadioLib 7.1.0
(Watch/Ultra/Plus) and 6.4.2 (Pro) APIs require separate CRC/IRQ compatibility
branches; builds and hardware checks must cover both.

Outgoing MeshCore text and advertisements require a valid system Unix clock
(2020 or later). Set the device time or allow the existing time-sync flow to set
it; incoming radio advertisements do not set the clock.

Radio changes are saved for the next boot, never applied during a live packet.
Identity and channel keys remain on the device. This change does not enable key
export, encrypt the device's NVS partition, or protect against physical extraction
from an unlocked device. Default public channels are shared with other users of
that channel and should not carry confidential information.

## Source and license

MeshCore source and pin: `lib/meshcore/UPSTREAM.md`.
Ed25519 source and license: `lib/meshcore_ed25519/`.
Crypto dependency: `rweather/Crypto@0.4.0`.

SOS and CheckIn use best-effort radio text. Queue acceptance does not confirm a received message.

## XNODE BLE bridge contract

The existing XNODE service/characteristic UUIDs, chunk framing, `protocolVersion: 1`
and `{ "type": "commandName", "payload": { ... } }` JSON envelope are unchanged.
These additions are the XNODE JSON bridge, not the native MeshCore companion BLE
protocol. All protocol identifiers on this wire are lowercase `meshtastic` or
`meshcore`; human-facing labels use Meshtastic/MeshCore.

### Discovery and protocol selection

`hello` still replies with `helloAck`. The payload now includes:

- `meshProtocolsAvailable`: supported identifiers (both on the four LoRa targets)
- `meshProtocolActive`: immutable radio owner for this boot
- `meshProtocolSelected`: persisted next-boot selection
- `protocolRequiresRestart`: selected differs from active
- `radioRequiresRestart`: MeshCore staged RF settings differ from live settings
- `meshMode`: `channel-mesh` for Meshtastic or `group-broadcast` for MeshCore
- `nativeMeshcoreCompanion`: `false`
- `meshMutationRequiresPairing`: `true`
- Existing `meshReady`, `meshStatus`, device, location and XTOC unit fields

Meshtastic active mode retains `nodeId` and `meshtasticUser` exactly as the legacy
client expects. MeshCore active mode omits both, returning `meshcoreUser` with
`publicKey` (all 64 hexadecimal Ed25519 public-key characters),
`idType: "ed25519-public-key"`, and `longName`, plus `meshcoreRadioActive` and
`meshcoreRadioSelected`. A MeshCore display-only numeric handle is never exposed
as a Meshtastic node ID. Radio configuration is loaded lazily from the independent MeshCore store before
configuration access; only the boot-selected backend starts the physical radio.
MeshCore-only configuration commands are rejected while Meshtastic is active.

The `capabilities` array advertises the active protocol and includes
`meshProtocolSelection`, `meshSend`, `meshChannels`, plus
`meshtasticNodeConfig` or `meshcoreNodeConfig` as appropriate. MeshCore adds
`meshcoreRadioConfig`. Existing sync/location/basemap/notification/SOS capabilities
remain available.

- `getMeshProtocol`, payload `{}`, replies `meshProtocolAck`
- `setMeshProtocol`, payload `{ "protocol": "meshcore" }`, replies
  `meshProtocolAck`

Both replies include the protocol state above, `ok`, `error` (empty on success),
`restarted: false`, and `restartAction: "device-radio-restart"`. Selection is saved
only after an exact supported string is validated and storage succeeds. Unknown
strings, numeric enum values and incorrect capitalization are rejected. A supplied
`reboot` field must be boolean `false`; `true` or another type is rejected before
any setting changes. There is deliberately no remote restart command. On the
physical device, open Mesh > RADIO, then Restart and confirm. Restart clears
volatile history/drafts and can lose pending transmissions. The active backend
and all current communication remain unchanged until that restart. Saving the
active protocol again cancels a pending protocol change.

### Protocol-aware broadcast, channel selection and RX

- `meshSend`: `{ "protocol": "meshcore", "text": "Hello" }` replies `meshSendAck`
- `selectMeshChannel`: `{ "protocol": "meshcore", "index": 0 }` replies
  `meshChannelAck`
- `getMeshChannels`: `{ "protocol": "meshcore" }` replies `meshChannelsAck`

All three require `protocol` to match the **active** backend. `meshSend` uses the
currently active channel, rejects empty/whitespace-only text and text above 200
bytes (the backend may enforce a lower bound for its packet overhead), and is
broadcast-only. Destination fields (`to`, `dest`, `destination`, `toNode`,
`to_node`, `nodeId`, `publicKey`) are rejected. Channel fields on `meshSend`
(`channel`, `channelIndex`, `channelSlot`) are rejected; select the channel first.
The send reply includes `ok`, `error`, `protocol`, `status`, `channelIndex`,
`channelSlot`, `queued`, and `delivered: false`. `ok`/`queued` means accepted for
local transmission, never remote reception or delivery confirmation.

Channel indexes are unsigned dropdown indexes over enabled channels, not
persistent slots. A selection reply includes `ok`, `error`, `protocol`, `status`,
`channelIndex`, `channelSlot`, and `activeIndex`. A channels reply includes `ok`,
`error`, `protocol`, `activeIndex`, and `channels`, whose entries contain `index`,
`slot`, `name`, and `keyBytes`. It never returns channel keys.

In Meshtastic mode, incoming radio text keeps the legacy `meshtasticRx` event;
in MeshCore mode it uses `meshRx`. Both payloads contain `from`, `text`, `ts`
(device uptime seconds), `protocol` and `mode`. MeshCore `from` is the group
message's unverified sender label, not an authenticated public key. The same
protocol-aware event names can be sent from a host for device notification;
`meshRx` requires a matching payload `protocol`. Legacy `meshtasticRx` input is
ignored while MeshCore is active. Group broadcasts have no delivery ACK. Native
MeshCore direct messages are unsupported in both RX and TX directions.

### MeshCore identity and channel configuration

`setMeshcoreUser` accepts `{ "longName": "Field node", "broadcast": true }` and
replies `meshcoreUserAck` with `ok`, `error`, `broadcastQueued`, and the current
`meshcoreUser` when that backend is active. `longName` must be 1–31 UTF-8 bytes
without colons or ASCII control characters. `broadcast` is optional and must be boolean;
a successful name save already schedules an advertisement, and `broadcast: true`
expedites that scheduled advertisement to 250 ms. `broadcastQueued` is therefore
true after any successful name save. Meshtastic fields
`shortName`, `isLicensed`, `isUnmessageable` are rejected. A queued advertisement
is not evidence another node received it.

`setMeshcoreChannel` accepts a complete enabled-channel replacement:

```json
{
  "protocol": "meshcore",
  "slot": 1,
  "enabled": true,
  "name": "Team",
  "keyHex": "000102030405060708090a0b0c0d0e0f"
}
```

The reply is `meshChannelsAck` as above. `slot` must be an unsigned integer 0–7;
`enabled` must be boolean. Enabled channels require a nonempty name of at most
15 bytes without colons or ASCII controls, and a key of exactly 32 or 64 valid hexadecimal
characters (16 or 32 bytes). To disable a slot, send its `slot`, `protocol`, and
`enabled: false`. Slot 0 is the required primary channel and cannot be disabled;
disabling another active slot selects slot 0. Short Meshtastic PSK indices are not accepted as MeshCore keys.
Configuration is saved in the MeshCore store, independently of Meshtastic.

### MeshCore RF profile

`getMeshcoreRadio` accepts `{}`. `setMeshcoreRadio` requires all five fields:

```json
{
  "frequencyMhz": 910.525,
  "bandwidthKhz": 62.5,
  "spreadingFactor": 7,
  "codingRate": 5,
  "txPowerDbm": 22
}
```

These commands require MeshCore to be active. Both reply `meshcoreRadioAck` with
`ok`, `error`, `restarted: false`, `radioRequiresRestart`, `active`, and `selected`.
Each profile object uses the five field names above. A successful setter persists
the next-boot profile and does not retune a live modem. Use the explicit on-device
Restart action to apply it. The UI shows the **live** profile until then.

Bandwidth is 62.5, 125 or 250 kHz; spreading factor is integer 7–12; coding rate is
integer 5–8 (the denominator in 4/5 to 4/8); transmit power is integer -9 to 22 dBm.
The complete occupied band must stay within 902–928 MHz. This is the existing
hardware/build region range, not a claim of legal approval for every location.
The band, modulation and channel key must match the intended MeshCore peers.

### Legacy isolation and XTOC packets

When MeshCore is active, the native Meshtastic protobuf GATT service is neither
created nor advertised; its command callback also guards against the wrong
active protocol. `setMeshtasticUser` returns `meshtasticUserAck` with `ok: false`,
`broadcastQueued: false`, and `error: "protocol-mismatch"`; it never changes the
MeshCore identity. The Meshtastic Bluetooth pairing preference is not applied as
a MeshCore protocol setting.

XTOC manual SOS and check-in still use the common service facade and selected
channel. Success notifications now say **Queued … Delivery unconfirmed** for
both protocols; readiness/send failures return false and identify the active
protocol. These group broadcasts do not become MeshCore direct messages merely
because the XTOC text names a destination Unit ID.

### BLE provisioning security

All **new mutating commands** (`setMeshProtocol`, `meshSend`, `selectMeshChannel`,
`setMeshcoreChannel`, `setMeshcoreUser`, `setMeshcoreRadio`) require an encrypted,
MITM-authenticated BLE connection. Otherwise the normal command-specific reply
contains `ok: false` and `error: "pairing-required"` and no mesh mutation occurs.
Clients must complete the device's existing passkey pairing flow **before sending
any channel key**. Checking or saving the protocol on the physical RADIO screen
remains available without a connected client.

Authentication is captured from the NimBLE connection descriptor on every frame.
Reassembly rejects mixed connection handles, authentication states, or connection
epochs; a disconnect invalidates queued and partial commands. Frame numbers are
strict decimal integers from 1 through 8192 with at most four digits; indexes
cannot exceed the declared total. Empty/overlong IDs (maximum 23 bytes), malformed
numbers and assemblies above the existing 8192 encoded-byte limit are rejected. A global UI
"connected" flag is never used as proof of authentication. The XNODE RX
characteristic is now write-only and clears its value after copying an incoming
frame; clients must read responses from TX, not read back their writes.

These changes are intentionally limited to the new mesh mutations. Legacy XNODE
commands and discovery retain their existing access policy and are not claimed to
be a secure general-purpose bridge. A client that sends a secret before pairing
has already transmitted it unencrypted even though the mutation will be rejected.
Provision only with a trusted client/environment, verify pairing first, and do not
enable verbose third-party transport logging while handling channel keys. Keys
are not included in replies or exported through channel discovery.

### Separate safety fix: peer positions cannot replace this device's emergency position

The pre-existing shared location helper stored every reported coordinate as the
device's own SOS/check-in position. A received Meshtastic position packet could
therefore replace the locally configured/GPS position; using the same helper for
MeshCore advertisements would have carried that error into the new backend.

Both radio backends now route received peer coordinates through a separate,
non-mutating helper. It emits **`meshPeerLocation`**, with `lat`, `lon`, `label`,
`ts` (uptime seconds), `protocol` (`meshtastic` or `meshcore`) and
`source: "mesh-peer"`. MeshCore advertisement reports also include all 64
hexadecimal characters of the peer's `publicKey` and `idType:
"ed25519-public-key"`, retained from the signature-verified advertisement; no
private key or truncated numeric identity is sent. Meshtastic peer reports omit
those MeshCore identity fields. It never changes the local SOS/check-in coordinates,
location-valid flag, or persisted location. The separate event name prevents
older clients that treat `location` as this device's position from confusing a
peer with the local device. Clients may opt in to the new peer event. On-device
peer map rendering is unchanged.

The existing **`location`** event remains for genuine local/GPS updates, which
still update the device's own emergency position. Both outgoing helpers reject
non-finite or out-of-range coordinates.
