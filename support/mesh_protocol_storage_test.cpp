// Compiles the production NVS-backed service against deterministic platform
// fixtures. This does not claim to exercise ESP32 flash hardware.
#include "app/mesh/mesh_protocol.h"
#include <Preferences.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace fake_nvs {
int value = -1;
unsigned writes = 0;
bool allow_open = true;
bool allow_write = true;
}

int main(int argc, char **argv) {
    fake_nvs::value = argc > 1 ? atoi(argv[1]) : -1;
    const auto initial = fake_nvs::value == 1 ? MESH_PROTOCOL_MESHCORE : MESH_PROTOCOL_MESHTASTIC;
    const auto other = initial == MESH_PROTOCOL_MESHCORE ? MESH_PROTOCOL_MESHTASTIC : MESH_PROTOCOL_MESHCORE;
    assert(mesh_protocol_get_active() == initial);
    assert(mesh_protocol_get_selected() == initial);
    assert(!mesh_protocol_reboot_required());
    assert(mesh_protocol_is_supported(MESH_PROTOCOL_MESHCORE));
    assert(!mesh_protocol_is_supported(static_cast<mesh_protocol_t>(255)));
    assert(!strcmp(mesh_protocol_name(MESH_PROTOCOL_MESHCORE), "MeshCore"));
    assert(!strcmp(mesh_protocol_name(static_cast<mesh_protocol_t>(9)), "Unknown"));
    assert(mesh_protocol_select(initial) && fake_nvs::writes == 0);
    assert(!mesh_protocol_select(static_cast<mesh_protocol_t>(9)) && fake_nvs::writes == 0);
    fake_nvs::allow_open = false;
    assert(!mesh_protocol_select(other));
    assert(mesh_protocol_get_selected() == initial);
    fake_nvs::allow_open = true;
    fake_nvs::allow_write = false;
    assert(!mesh_protocol_select(other));
    assert(!mesh_protocol_reboot_required());
    fake_nvs::allow_write = true;
    assert(mesh_protocol_select(other));
    assert(fake_nvs::value == other && mesh_protocol_reboot_required());
    assert(mesh_protocol_get_active() == initial); // Radio remains untouched.
    mesh_protocol_initialize();
    assert(mesh_protocol_get_active() == initial); // Does not reload saved mode.
    assert(mesh_protocol_select(initial));
    assert(!mesh_protocol_reboot_required() && fake_nvs::value == initial);
    printf("Production NVS protocol service: persisted=%s, failure/retry/staging/cancel PASS\n", argc > 1 ? argv[1] : "missing");
}
