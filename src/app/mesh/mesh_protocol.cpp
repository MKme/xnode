#include "mesh_protocol.h"
#include "mesh_protocol_state.h"

#if !defined(NATIVE_64BIT) && (defined(USING_TWATCH_S3) || defined(USING_TWATCH_ULTRA) || defined(USING_TDECK_PLUS) || defined(USING_TDECK_PRO))
#define XNODE_DUAL_MESH 1
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace {
MeshProtocolSelection selection;

#ifdef XNODE_DUAL_MESH
StaticSemaphore_t mutex_storage;
SemaphoreHandle_t protocol_mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
struct ProtocolLock {
    ProtocolLock() { xSemaphoreTake(protocol_mutex, portMAX_DELAY); }
    ~ProtocolLock() { xSemaphoreGive(protocol_mutex); }
};

uint8_t read_selection() {
    Preferences prefs;
    // NVS is separate from both protocols' channel and identity stores. A
    // missing namespace is normal on upgrade and retains Meshtastic behavior.
    if (!prefs.begin("xnode-mesh", true)) return MESH_PROTOCOL_MESHTASTIC;
    const uint8_t value = prefs.getUChar("protocol", MESH_PROTOCOL_MESHTASTIC);
    prefs.end();
    return value;
}
bool save_selection(mesh_protocol_t protocol) {
    Preferences prefs;
    if (!prefs.begin("xnode-mesh", false)) return false;
    const bool saved = prefs.putUChar("protocol", static_cast<uint8_t>(protocol)) == 1 &&
        prefs.getUChar("protocol", 0xff) == static_cast<uint8_t>(protocol);
    prefs.end();
    return saved;
}
#else
struct ProtocolLock { ProtocolLock() {} ~ProtocolLock() {} };
uint8_t read_selection() { return MESH_PROTOCOL_MESHTASTIC; }
bool save_selection(mesh_protocol_t) { return false; }
#endif

void initialize_locked() {
    if (selection.initialized()) return;
#ifdef XNODE_DUAL_MESH
    selection.initialize(read_selection(), true);
#else
    selection.initialize(read_selection(), false);
#endif
}
}

void mesh_protocol_initialize(void) {
    ProtocolLock lock;
    initialize_locked();
}
mesh_protocol_t mesh_protocol_get_active(void) {
    ProtocolLock lock;
    initialize_locked();
    return selection.active();
}
mesh_protocol_t mesh_protocol_get_selected(void) {
    ProtocolLock lock;
    initialize_locked();
    return selection.selected();
}
const char *mesh_protocol_name(mesh_protocol_t protocol) {
    switch (protocol) {
        case MESH_PROTOCOL_MESHTASTIC: return "Meshtastic";
        case MESH_PROTOCOL_MESHCORE: return "MeshCore";
        default: return "Unknown";
    }
}
bool mesh_protocol_is_supported(mesh_protocol_t protocol) {
    ProtocolLock lock;
    initialize_locked();
    return selection.supported(protocol);
}
bool mesh_protocol_select(mesh_protocol_t protocol) {
    ProtocolLock lock;
    initialize_locked();
    return selection.select(protocol, save_selection);
}
bool mesh_protocol_reboot_required(void) {
    ProtocolLock lock;
    initialize_locked();
    return selection.reboot_required();
}
