# MeshCore provenance

Source: https://github.com/meshcore-dev/MeshCore
Commit: a366955cb2f67b8e6842d4f00d2b6a554dddd88a
Library metadata version: 1.10.0
License: upstream MIT (LICENSE).

This is an unmodified subset of upstream `src`: core packet, identity,
dispatcher, mesh and crypto utilities plus BaseChatMesh, advertisement/text,
contact/channel and deduplication helpers. Boards, example firmware, BLE stacks,
serial protocols, display drivers and upstream RadioLib wrappers are excluded.
XNODE supplies its own board-HAL SX1262 adapter, fixed-capacity packet manager,
NVS persistence and UI/BLE bridge under `src/app/meshcore`.

Do not define MAX_GROUP_CHANNELS: XNODE supplies channel matching and therefore
requires no Base64 dependency. rweather/Crypto is pinned to 0.4.0. The upstream
Ed25519 C implementation is separately vendored in `lib/meshcore_ed25519` with
its original zlib-style license. Neither library is fetched from a moving branch.
