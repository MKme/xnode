#include "mesh_history.h"
#include <string.h>

uint32_t MeshHistory::add(const mesh_message_t &message) {
    if (!message.text[0]) return 0;
    // Meshtastic retransmissions retain the source/id pair. Keep one bubble per
    // received packet on its actual channel; id zero is not a usable dedupe key.
    if (!message.outgoing && message.packet_id) {
        for (size_t i = 0; i < size_; ++i) {
            const mesh_message_t &existing = messages_[(head_ + i) % CAPACITY];
            if (!existing.outgoing && existing.from_node == message.from_node &&
                existing.packet_id == message.packet_id && existing.channel_slot == message.channel_slot)
                return existing.sequence;
        }
    }
    const size_t slot = (head_ + size_) % CAPACITY;
    if (size_ == CAPACITY) head_ = (head_ + 1) % CAPACITY;
    else ++size_;
    messages_[slot] = message;
    messages_[slot].sender[sizeof(messages_[slot].sender) - 1] = '\0';
    messages_[slot].text[sizeof(messages_[slot].text) - 1] = '\0';
    if (++sequence_ == 0) ++sequence_;
    messages_[slot].sequence = sequence_;
    ++revision_;
    return sequence_;
}

bool MeshHistory::set_status(uint32_t sequence, mesh_message_status_t status) {
    if (!sequence) return false;
    for (size_t i = 0; i < size_; ++i) {
        mesh_message_t &message = messages_[(head_ + i) % CAPACITY];
        if (message.sequence != sequence) continue;
        // Only a pending local transmission can complete or fail. Late callbacks
        // must not resurrect failed attempts or change received messages.
        if (!message.outgoing || message.status != MESH_MESSAGE_QUEUED ||
            (status != MESH_MESSAGE_TRANSMITTED && status != MESH_MESSAGE_FAILED)) return false;
        message.status = status;
        ++revision_;
        return true;
    }
    return false;
}

bool MeshHistory::acknowledge(uint32_t packet_id, uint8_t channel_slot, uint32_t local_node,
                              uint32_t peer, const char *rebroadcast_text) {
    if (!packet_id || !local_node || !peer || peer == 0xffffffffu) return false;
    if (!rebroadcast_text && peer == local_node) return false;
    for (size_t i = 0; i < size_; ++i) {
        mesh_message_t &message = messages_[(head_ + i) % CAPACITY];
        if (!message.outgoing || message.packet_id != packet_id || message.channel_slot != channel_slot ||
            message.from_node != local_node || message.status == MESH_MESSAGE_ACKNOWLEDGED) continue;
        if (rebroadcast_text) {
            if (peer != local_node || message.to_node != 0xffffffffu || strcmp(message.text, rebroadcast_text)) continue;
        } else if (message.to_node != 0xffffffffu && message.to_node != peer) continue;
        message.status = MESH_MESSAGE_ACKNOWLEDGED;
        ++revision_;
        return true;
    }
    return false;
}

size_t MeshHistory::count(uint8_t channel_slot) const {
    size_t result = 0;
    for (size_t i = 0; i < size_; ++i)
        if (messages_[(head_ + i) % CAPACITY].channel_slot == channel_slot) ++result;
    return result;
}

bool MeshHistory::get(uint8_t channel_slot, size_t chronological_index, mesh_message_t *out) const {
    if (!out) return false;
    for (size_t i = 0; i < size_; ++i) {
        const mesh_message_t &message = messages_[(head_ + i) % CAPACITY];
        if (message.channel_slot != channel_slot) continue;
        if (chronological_index-- == 0) { *out = message; return true; }
    }
    return false;
}
