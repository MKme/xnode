#pragma once
#include <Dispatcher.h>
#include "meshcore_logic.h"

// No allocation after startup, no unbounded queues. The same fixed packet pool
// backs receive, transmit and dispatcher delayed queues.
class MeshCorePacketPool : public mesh::PacketManager {
public:
    static constexpr unsigned CAPACITY = 12;
    mesh::Packet* allocNew() override {
        for (unsigned i = 0; i < CAPACITY; ++i) if (!used_[i]) {
            used_[i] = true;
            packets_[i].header = packets_[i].payload_len = packets_[i].path_len = 0;
            packets_[i]._snr = 0;
            memset(packets_[i].transport_codes, 0, sizeof(packets_[i].transport_codes));
            memset(packets_[i].path, 0, sizeof(packets_[i].path));
            memset(packets_[i].payload, 0, sizeof(packets_[i].payload));
            return &packets_[i];
        }
        return nullptr;
    }
    void free(mesh::Packet *packet) override {
        for (unsigned i = 0; i < CAPACITY; ++i) if (packet == &packets_[i]) {
            used_[i] = false;
            for (auto &entry : tx_) if (entry.packet == packet) entry.packet = nullptr;
            for (auto &entry : rx_) if (entry.packet == packet) entry.packet = nullptr;
            return;
        }
    }
    void queueOutbound(mesh::Packet *p, uint8_t priority, uint32_t due) override { enqueue(tx_, p, priority, due); }
    void queueInbound(mesh::Packet *p, uint32_t due) override { enqueue(rx_, p, 0, due); }
    mesh::Packet *getNextOutbound(uint32_t now) override { return next(tx_, now); }
    mesh::Packet *getNextInbound(uint32_t now) override { return next(rx_, now); }
    int getOutboundCount(uint32_t now) const override {
        int n = 0; for (const auto &e : tx_) if (e.packet && xnode_meshcore::time_due(now, e.due)) ++n; return n;
    }
    int getOutboundTotal() const override { int n = 0; for (const auto &e : tx_) if (e.packet) ++n; return n; }
    int getFreeCount() const override { int n = 0; for (bool u : used_) if (!u) ++n; return n; }
    mesh::Packet *getOutboundByIdx(int index) override {
        for (auto &e : tx_) if (e.packet && index-- == 0) return e.packet; return nullptr;
    }
    mesh::Packet *removeOutboundByIdx(int index) override {
        for (auto &e : tx_) if (e.packet && index-- == 0) { auto p = e.packet; e.packet = nullptr; return p; } return nullptr;
    }
private:
    struct Entry { mesh::Packet *packet = nullptr; uint32_t due = 0; uint8_t priority = 0; };
    mesh::Packet packets_[CAPACITY];
    bool used_[CAPACITY] = {};
    Entry tx_[CAPACITY], rx_[CAPACITY];
    void enqueue(Entry *entries, mesh::Packet *p, uint8_t priority, uint32_t due) {
        // A queue cannot fill while a newly allocated pool packet is held.
        for (unsigned i = 0; i < CAPACITY; ++i) if (!entries[i].packet) {
            entries[i].packet = p; entries[i].priority = priority; entries[i].due = due; return;
        }
        free(p);
    }
    mesh::Packet *next(Entry *entries, uint32_t now) {
        int best = -1;
        for (unsigned i = 0; i < CAPACITY; ++i) if (entries[i].packet && xnode_meshcore::time_due(now, entries[i].due))
            if (best < 0 || entries[i].priority < entries[best].priority ||
                (entries[i].priority == entries[best].priority && (int32_t)(entries[i].due - entries[best].due) < 0)) best = i;
        if (best < 0) return nullptr;
        auto p = entries[best].packet; entries[best].packet = nullptr; return p;
    }
};
