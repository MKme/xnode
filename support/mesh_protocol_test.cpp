#include "app/mesh/mesh_protocol_state.h"
#include <assert.h>
#include <stdio.h>

static unsigned writes;
static bool can_save = true;
static uint8_t stored;
static bool save(mesh_protocol_t protocol) {
    ++writes;
    if (!can_save) return false;
    stored = protocol;
    return true;
}

int main() {
    MeshProtocolSelection uninitialized;
    assert(!uninitialized.select(MESH_PROTOCOL_MESHCORE, save));
    for (unsigned raw = 0; raw < 256; ++raw) {
        MeshProtocolSelection state;
        state.initialize(raw, true);
        const auto expected = raw == 1 ? MESH_PROTOCOL_MESHCORE : MESH_PROTOCOL_MESHTASTIC;
        assert(state.active() == expected && state.selected() == expected);
        assert(!state.reboot_required());
        state.initialize(1 - expected, true); // Reinitialization cannot hot-swap.
        assert(state.active() == expected);
    }

    MeshProtocolSelection state;
    state.initialize(0, true);
    assert(state.supported(MESH_PROTOCOL_MESHTASTIC) && state.supported(MESH_PROTOCOL_MESHCORE));
    assert(state.select(MESH_PROTOCOL_MESHTASTIC, save) && writes == 0);
    assert(!state.select(static_cast<mesh_protocol_t>(2), save) && writes == 0);
    assert(!state.select(MESH_PROTOCOL_MESHCORE, nullptr));
    can_save = false;
    assert(!state.select(MESH_PROTOCOL_MESHCORE, save));
    assert(state.active() == MESH_PROTOCOL_MESHTASTIC && !state.reboot_required());
    can_save = true;
    assert(state.select(MESH_PROTOCOL_MESHCORE, save));
    assert(stored == MESH_PROTOCOL_MESHCORE && state.reboot_required());
    assert(state.active() == MESH_PROTOCOL_MESHTASTIC); // Still owns radio.
    const auto committed_writes = writes;
    assert(state.select(MESH_PROTOCOL_MESHCORE, save) && writes == committed_writes);

    MeshProtocolSelection rebooted;
    rebooted.initialize(stored, true);
    assert(rebooted.active() == MESH_PROTOCOL_MESHCORE && !rebooted.reboot_required());
    assert(rebooted.select(MESH_PROTOCOL_MESHTASTIC, save));
    assert(rebooted.active() == MESH_PROTOCOL_MESHCORE && rebooted.reboot_required());
    assert(rebooted.select(MESH_PROTOCOL_MESHCORE, save));
    assert(!rebooted.reboot_required()); // Cancel staged change, persisted too.

    MeshProtocolSelection unsupported;
    unsupported.initialize(1, false);
    assert(unsupported.active() == MESH_PROTOCOL_MESHTASTIC);
    assert(!unsupported.supported(MESH_PROTOCOL_MESHCORE));
    assert(!unsupported.select(MESH_PROTOCOL_MESHCORE, save));
    puts("Protocol selection: valid/corrupt persistence, write failure, immutable active mode, cancellation and unsupported targets PASS");
}
