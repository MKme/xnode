#ifndef XNODE_MESH_PROTOCOL_STATE_H
#define XNODE_MESH_PROTOCOL_STATE_H

#include "mesh_protocol.h"

// Hardware-independent policy, shared by production and native tests. Storage
// and synchronization belong to the service; failed persistence is never a
// successful switch. Invalid/unsupported persisted values default to Meshtastic.
class MeshProtocolSelection {
public:
    using Save = bool (*)(mesh_protocol_t);

    void initialize(uint8_t persisted, bool supports_meshcore) {
        if (initialized_) return;
        supports_meshcore_ = supports_meshcore;
        active_ = persisted == MESH_PROTOCOL_MESHCORE && supports_meshcore
            ? MESH_PROTOCOL_MESHCORE : MESH_PROTOCOL_MESHTASTIC;
        selected_ = active_;
        initialized_ = true;
    }
    bool supported(mesh_protocol_t protocol) const {
        return protocol == MESH_PROTOCOL_MESHTASTIC ||
            (protocol == MESH_PROTOCOL_MESHCORE && supports_meshcore_);
    }
    bool select(mesh_protocol_t protocol, Save save) {
        if (!initialized_ || !supported(protocol)) return false;
        if (selected_ == protocol) return true;
        if (!save || !save(protocol)) return false;
        selected_ = protocol;
        return true;
    }
    mesh_protocol_t active() const { return active_; }
    mesh_protocol_t selected() const { return selected_; }
    bool reboot_required() const { return active_ != selected_; }
    bool initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    bool supports_meshcore_ = false;
    mesh_protocol_t active_ = MESH_PROTOCOL_MESHTASTIC;
    mesh_protocol_t selected_ = MESH_PROTOCOL_MESHTASTIC;
};

#endif
