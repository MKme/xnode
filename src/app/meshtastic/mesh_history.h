#ifndef XNODE_MESH_HISTORY_H
#define XNODE_MESH_HISTORY_H

#include <stddef.h>
#include <stdint.h>

enum mesh_message_status_t : uint8_t {
    MESH_MESSAGE_RECEIVED,
    MESH_MESSAGE_QUEUED,
    // The local radio reported TX_DONE. This is not a recipient acknowledgement.
    MESH_MESSAGE_TRANSMITTED,
    MESH_MESSAGE_FAILED,
    // A correlated over-the-air routing ACK or broadcast rebroadcast was received.
    MESH_MESSAGE_ACKNOWLEDGED
};

struct mesh_message_t {
    uint32_t sequence;
    uint32_t from_node;
    uint32_t to_node;
    uint32_t packet_id;
    uint32_t timestamp; // Local clock at observation; zero when the clock is unset.
    uint32_t uptime_ms;
    uint8_t channel_slot;
    bool outgoing;
    mesh_message_status_t status;
    char sender[40];
    char text[201];
};

// Volatile, bounded history shared by all channels. The service serializes access;
// this standalone container has no hardware, storage, radio or heap dependency.
class MeshHistory {
public:
    static constexpr size_t CAPACITY = 24;
    uint32_t add(const mesh_message_t &message);
    bool set_status(uint32_t sequence, mesh_message_status_t status);
    bool acknowledge(uint32_t packet_id, uint8_t channel_slot, uint32_t local_node,
                     uint32_t peer, const char *rebroadcast_text = nullptr);
    size_t count(uint8_t channel_slot) const;
    bool get(uint8_t channel_slot, size_t chronological_index, mesh_message_t *out) const;
    uint32_t revision() const { return revision_; }
private:
    mesh_message_t messages_[CAPACITY] = {};
    size_t head_ = 0;
    size_t size_ = 0;
    uint32_t sequence_ = 0;
    uint32_t revision_ = 0;
};

#endif
