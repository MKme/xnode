# XNODE MeshCore backend

The active-board implementation is a companion/group-chat client backed by the
unmodified, pinned MeshCore `BaseChatMesh`, `Mesh` and `Dispatcher`. It supports
T-Watch S3, T-Watch Ultra, T-Deck Plus and T-Deck Pro using each board's existing
`newModule()` SX1262 HAL. It does not claim MeshCore's companion BLE/USB protocol:
notifications and control go through XNODE's existing bridge. Only the selected
protocol calls setup; protocol changes require a reboot. Constructing the radio
is deferred until selected setup.

## Interoperability and regional settings

Default radio: 910.525 MHz, 62.5 kHz bandwidth, SF7, CR5, private LoRa sync word,
CRC on, TX power 22 dBm. The preamble is upstream's 32 symbols for SF7/8 and 16
symbols above SF8. This is a US/CAN-oriented profile within this firmware's
existing 902–928 MHz band. It must match nearby peers and is not a worldwide
preset or a guarantee of regulatory compliance. Channel changes do not change
the frequency. Radio profile changes are strictly validated, stored separately
and take effect only after reboot. The upstream companion airtime budget factor
1.0 is retained; the backend is not a repeater.

The default Public channel uses the official companion PSK
`8b3387e9c5cdea6ac9e5edbaa115cd72`. Eight independently enabled slots accept
16-byte or 32-byte PSKs. Primary slot 0 stays enabled. Group payloads have
160 bytes total for `name: message`; the service rejects oversize input rather
than silently truncating it. Names have a 31-byte limit. All limits count UTF-8
bytes, not visible characters.

Only valid v1 advertisements and group text/data envelopes are passed into the
upstream dispatcher. Envelopes are checked for minimum headers, optional
transport codes, legal path encodings, bounds, supported payload types and whole
AES blocks before parsing. Advertisements are signature-verified upstream.
Received advertisements with valid positions update the existing map marker.
Self advertisements currently include the name, not a local position. The
bounded upstream contact table retains all 32 public-key bytes in RAM.

Group sender labels are unverified names chosen by holders of the channel key;
they are not Ed25519-authenticated sender identities. Legacy callback `from_node`
is therefore zero for group RX. The local legacy ID is a display-only handle.
`get_public_key_hex()` exposes the complete 64-hex-character Ed25519 identity.
All legacy non-broadcast sends are rejected; no public key is truncated to route
a DM. DM/path/control packets are intentionally outside this group-only build.

## Identity, storage and time

NVS namespace `meshcore` is separate from Meshtastic. A 96-byte upstream identity
blob preserves the complete private/public pair, with checked write and readback
before readiness. Existing malformed/mismatched identities fail closed and are
never silently replaced. New keys mix ESP hardware RNG output with SX1262 radio
noise before reception starts. Reserved 0/FF one-byte prefixes are rejected.
Secrets are not logged or returned through public getters. NVS is not claimed to
be encrypted unless platform flash/NVS encryption is separately configured.

A separate versioned config blob stores name, channels, active slot and the
staged radio profile. Failed/corrupt reads do not overwrite existing data. All
config operations are serialized by a recursive mutex, including inactive
protocol reads. They never create an identity or initialize the radio. Radio
configuration is not live-swapped during TX.

The normal system Unix clock supplies message/advert timestamps. When it is
unset (earlier than 2020), outgoing traffic is refused until the clock is set;
received advertisements cannot change the clock. This avoids pretending uptime
is a Unix timestamp. Boot-relative millis is used only for scheduling/timeouts.

## Resource and delivery semantics

Twelve fixed pool packets share bounded RX/TX queues; the core contact/dedupe
arrays are bounded. Shared 24-entry history and six deferred delivery records
have no unbounded growth. The delivery batch lives in static memory, keeping
it off the radio/crypto call stack. Power-manager callbacks keep the MCU and
radio available during display-off standby. Radio and history operations share
a recursive mutex. External notifications, map updates and BLE/user callbacks
run only after it is released, with RX restored.

Queued/transmitted/failed history follows dispatcher callbacks. `transmitted`
requires SX1262 TX_DONE, no timeout, and a successful finishTransmit; this is
not proof of receipt. Broadcasts are never marked acknowledged, and a relayed
copy is never treated as an ACK. RX length/CRC failures, startup failures and
storage errors are not fabricated as successful traffic.

## Test runners

Run `python3 src/app/meshcore/test_native.py` for input-boundary and packet-pool assertions. The Packet.cpp link hash stub is not a crypto implementation. Run `python3 src/app/meshcore/test_crypto.py --crypto-dir <installed-Crypto-directory>` with an existing compiler and Python cryptography for independent crypto-vector comparisons. The crypto runner verifies installed Crypto version metadata and performs no downloads. Omit the option to use CRYPTO_DIR or the existing PlatformIO dependency install.
