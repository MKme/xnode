#include "app/meshtastic/mesh_history.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static mesh_message_t record(uint32_t id, uint8_t slot = 0, bool outgoing = false) {
    mesh_message_t result = {};
    result.from_node = 123;
    result.to_node = 0xffffffff;
    result.packet_id = id;
    result.channel_slot = slot;
    result.timestamp = 1780000000 + id;
    result.uptime_ms = 1000 + id;
    result.outgoing = outgoing;
    result.status = outgoing ? MESH_MESSAGE_QUEUED : MESH_MESSAGE_RECEIVED;
    strcpy(result.sender, outgoing ? "Me" : "FIELD 1");
    snprintf(result.text, sizeof(result.text), "Message %u", id);
    return result;
}

int main() {
    MeshHistory history;
    mesh_message_t got = {};
    assert(history.revision() == 0 && history.count(0) == 0);
    assert(!history.get(0, 0, &got) && !history.get(0, 0, nullptr));
    const auto incoming = record(15, 2);
    const uint32_t incoming_seq = history.add(incoming);
    assert(history.add(incoming) == incoming_seq && history.count(2) == 1 && history.revision() == 1);
    assert(!history.set_status(incoming_seq, MESH_MESSAGE_TRANSMITTED));
    assert(history.get(2, 0, &got));
    assert(got.timestamp == incoming.timestamp && got.uptime_ms == incoming.uptime_ms);
    assert(got.from_node == incoming.from_node && got.to_node == incoming.to_node);
    auto other = incoming;
    other.from_node = 456;
    assert(history.add(other) != incoming_seq); // Same id from a different sender is real data.
    other.channel_slot = 7;
    assert(history.add(other)); // Sparse channel slots never become dropdown indices.
    assert(history.count(0) == 0 && history.count(2) == 2 && history.count(7) == 1);
    assert(!history.get(2, 2, &got));
    auto pending = record(16, 2, true);
    uint32_t pending_seq = history.add(pending);
    assert(history.set_status(pending_seq, MESH_MESSAGE_TRANSMITTED));
    assert(!history.set_status(pending_seq, MESH_MESSAGE_FAILED));
    pending_seq = history.add(pending); // Repeated local sends are separate attempts.
    assert(history.set_status(pending_seq, MESH_MESSAGE_FAILED));
    assert(!history.set_status(pending_seq, MESH_MESSAGE_TRANSMITTED));
    assert(!history.set_status(0, MESH_MESSAGE_FAILED));
    auto rejected = record(0, 2, true);
    rejected.status = MESH_MESSAGE_FAILED;
    assert(history.add(rejected));
    assert(history.get(2, 4, &got) && got.status == MESH_MESSAGE_FAILED);
    auto no_id = record(0, 2);
    assert(history.add(no_id) != history.add(no_id));
    MeshHistory ring;
    for (uint32_t id = 1; id <= 100; ++id) ring.add(record(id, (id % 2) ? 2 : 7));
    assert(ring.count(2) == 12 && ring.count(7) == 12);
    assert(ring.get(2, 0, &got) && got.packet_id == 77);
    assert(ring.get(7, 11, &got) && got.packet_id == 100);
    assert(!ring.set_status(1, MESH_MESSAGE_FAILED)); // Eviction cannot mutate a new bubble.
    auto malformed = record(101);
    memset(malformed.sender, 's', sizeof(malformed.sender));
    memset(malformed.text, 't', sizeof(malformed.text));
    ring.add(malformed);
    assert(ring.get(0, 0, &got));
    assert(strlen(got.sender) == 39 && strlen(got.text) == 200);
    malformed.text[0] = 0;
    auto before = ring.revision();
    assert(ring.add(malformed) == 0 && ring.revision() == before);
    printf("Mesh history: bounded wrap, sparse channels, dedupe, metadata, TX outcomes, eviction and termination passed (%zu bytes).\n", sizeof(MeshHistory));
}
