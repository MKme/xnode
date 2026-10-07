# Ed25519 provenance

Copied without source changes from `lib/ed25519` in MeshCore commit
`a366955cb2f67b8e6842d4f00d2b6a554dddd88a`:
https://github.com/meshcore-dev/MeshCore/tree/a366955cb2f67b8e6842d4f00d2b6a554dddd88a/lib/ed25519

Original author: Orson Peters. The original zlib-style license is preserved in
`license.txt`. ED25519_NO_SEED=1 omits OS entropy helpers; XNODE supplies hardware
entropy. Library metadata is XNODE-specific. MeshCore's Identity.cpp requires
this fork's `ed25519_derive_pub` in addition to the original API.
