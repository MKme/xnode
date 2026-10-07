#ifndef XNODE_MESH_PROTOCOL_H
#define XNODE_MESH_PROTOCOL_H

#include <stdint.h>

enum mesh_protocol_t : uint8_t {
    MESH_PROTOCOL_MESHTASTIC = 0,
    MESH_PROTOCOL_MESHCORE = 1
};

// A selection is staged for the next boot. The active protocol never changes
// during a boot: only one radio driver and its power callbacks can own SX1262.
void mesh_protocol_initialize(void);
mesh_protocol_t mesh_protocol_get_active(void);
mesh_protocol_t mesh_protocol_get_selected(void);
const char *mesh_protocol_name(mesh_protocol_t protocol);
bool mesh_protocol_is_supported(mesh_protocol_t protocol);
bool mesh_protocol_select(mesh_protocol_t protocol);
bool mesh_protocol_reboot_required(void);

#endif
