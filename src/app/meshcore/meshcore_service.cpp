#include "config.h"
#include "meshcore_service.h"
#include "meshcore_logic.h"

#if !defined(NATIVE_64BIT) && (defined(USING_TWATCH_S3) || defined(USING_TWATCH_ULTRA) || defined(USING_TDECK_PLUS) || defined(USING_TDECK_PRO))
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include <stdarg.h>
#include <time.h>
#include "meshcore_packet_pool.h"
#if defined(USING_TWATCH_ULTRA)
#include "hardware/twatch_ultra_hal.h"
#elif defined(USING_TDECK_PLUS)
#include "hardware/tdeck_plus_hal.h"
#elif defined(USING_TDECK_PRO)
#include "hardware/tdeck_pro_hal.h"
#else
#include <LilyGoLib.h>
#endif
#include "hardware/powermgm.h"
#include "hardware/ble/xnode.h"
#include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
#include "app/osmmap/osmmap_app_main.h"

namespace {
using namespace xnode_meshcore;
constexpr uint32_t CONFIG_MAGIC = 0x4d430001;
constexpr uint32_t LOCAL_HANDLE = 0x4d430001; // UI handle, never a wire identity
constexpr uint32_t ADVERT_INTERVAL = 15UL * 60UL * 1000UL;
constexpr uint8_t PUBLIC_KEY[16] = {0x8b,0x33,0x87,0xe9,0xc5,0xcd,0xea,0x6a,0xc9,0xe5,0xed,0xba,0xa1,0x15,0xcd,0x72};
#if defined(USING_TWATCH_S3)
constexpr float TCXO_VOLTS = 3.0f;
#else
constexpr float TCXO_VOLTS = 1.8f;
#endif

struct Config {
    uint32_t magic;
    char name[32];
    char short_name[5];
    uint8_t active_slot;
    meshtastic_service_channel_info_t channels[CHANNEL_COUNT];
    meshcore_service_radio_config_t radio;
};
Config defaults() {
    Config c = {};
    c.magic = CONFIG_MAGIC;
    strcpy(c.name, "XNODE"); strcpy(c.short_name, "XND");
    c.channels[0].enabled = true;
    c.channels[0].role = MESHTASTIC_SERVICE_CHANNEL_ROLE_PRIMARY;
    strcpy(c.channels[0].name, "Public");
    c.channels[0].psk_len = sizeof(PUBLIC_KEY);
    memcpy(c.channels[0].psk, PUBLIC_KEY, sizeof(PUBLIC_KEY));
    c.radio = {910.525f, 62.5f, 7, 5, 22};
    return c;
}
Config config = defaults();
meshcore_service_radio_config_t active_radio = config.radio;
bool started = false, ready = false, config_loaded = false, config_attempted = false;
bool load_config();
bool advert_due = false;
uint32_t advert_at = 0;
char status_text[96] = "MeshCore idle";
char public_hex[65] = {};
char last_sender[40] = {}, last_text[201] = {};
uint32_t last_peer = 0;
int32_t last_rssi = 0;
float last_snr = 0;
meshtastic_service_text_rx_cb_t text_callback = nullptr;
MeshHistory history;
StaticSemaphore_t mutex_storage;
SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage);
struct Lock {
    Lock() {
        // Created once before application tasks, including on Arduino builds
        // using -fno-threadsafe-statics. Inactive config calls are serialized.
        if (mutex) xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
        if (!config_attempted) { config_attempted = true; load_config(); }
    }
    ~Lock() { if (mutex) xSemaphoreGiveRecursive(mutex); }
};
void status(const char *format, ...) {
    va_list args; va_start(args, format);
    vsnprintf(status_text, sizeof(status_text), format, args); va_end(args);
}
void hex_key(const uint8_t *key, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < PUB_KEY_SIZE; ++i) { out[i*2] = digits[key[i] >> 4]; out[i*2+1] = digits[key[i] & 15]; }
    out[64] = 0;
}
bool radio_valid(const meshcore_service_radio_config_t &r) {
    return valid_radio(r.frequency_mhz, r.bandwidth_khz, r.spreading_factor, r.coding_rate, r.tx_power_dbm);
}
bool radio_same(const meshcore_service_radio_config_t &a, const meshcore_service_radio_config_t &b) {
    return a.frequency_mhz == b.frequency_mhz && a.bandwidth_khz == b.bandwidth_khz &&
        a.spreading_factor == b.spreading_factor && a.coding_rate == b.coding_rate && a.tx_power_dbm == b.tx_power_dbm;
}
bool channel_valid(uint8_t slot, const meshtastic_service_channel_info_t &c) {
    if (slot >= CHANNEL_COUNT) return false;
    if (strnlen(c.name, sizeof(c.name)) >= sizeof(c.name) || c.psk_len > sizeof(c.psk)) return false;
    if (!c.enabled) return slot != 0 && c.role == MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED &&
        (c.psk_len == 0 || valid_key_length(c.psk_len));
    return valid_name(c.name, sizeof(c.name)) && valid_key_length(c.psk_len) &&
        c.role == (slot == 0 ? MESHTASTIC_SERVICE_CHANNEL_ROLE_PRIMARY : MESHTASTIC_SERVICE_CHANNEL_ROLE_SECONDARY);
}
bool config_valid(const Config &c) {
    if (c.magic != CONFIG_MAGIC || !valid_name(c.name, sizeof(c.name)) ||
        strnlen(c.short_name, sizeof(c.short_name)) >= sizeof(c.short_name) ||
        c.active_slot >= CHANNEL_COUNT || !c.channels[c.active_slot].enabled || !radio_valid(c.radio)) return false;
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) if (!channel_valid(i, c.channels[i])) return false;
    return true;
}
bool persist(const Config &c) {
    Preferences prefs;
    if (!prefs.begin("meshcore", false)) return false;
    const bool ok = prefs.putBytes("cfg_v1", &c, sizeof(c)) == sizeof(c);
    Config verify = {};
    const bool readback = prefs.getBytes("cfg_v1", &verify, sizeof(verify)) == sizeof(verify) && memcmp(&c, &verify, sizeof(c)) == 0;
    prefs.end();
    return ok && readback;
}
bool load_config() {
    Preferences prefs;
    if (!prefs.begin("meshcore", false)) { status("MeshCore storage unavailable"); return false; }
    const bool exists = prefs.isKey("cfg_v1");
    Config stored = {};
    const bool ok = !exists || (prefs.getBytesLength("cfg_v1") == sizeof(stored) &&
        prefs.getBytes("cfg_v1", &stored, sizeof(stored)) == sizeof(stored) && config_valid(stored));
    prefs.end();
    if (!ok) { status("MeshCore config invalid; retained for recovery"); return false; }
    if (exists) config = stored;
    else if (!persist(config)) { status("MeshCore config save failed"); return false; }
    active_radio = config.radio;
    config_loaded = true;
    return true;
}
uint32_t unix_time() { const time_t t = time(nullptr); return t >= 1577836800 ? (uint32_t)t : 0; }
class Clock : public mesh::MillisecondClock, public mesh::RTCClock {
public:
    unsigned long getMillis() override { return millis(); }
    uint32_t getCurrentTime() override { return unix_time(); }
    // Never trust radio advertisements to set the device's system clock.
    void setCurrentTime(uint32_t) override {}
} clock_source;
class Random : public mesh::RNG {
public:
    void random(uint8_t *out, size_t size) override { esp_fill_random(out, size); }
} random_source;
SX1262 &hardware_radio() { static SX1262 radio = newModule(); return radio; }
volatile bool radio_irq = false;
void IRAM_ATTR radio_isr() { radio_irq = true; powermgm_resume_from_ISR(); }

class Radio : public mesh::Radio {
    bool receiving_ = false, transmitting_ = false, finished_ = false;
    float rssi_ = 0, snr_ = 0;
    uint32_t rx_preamble_at_ = 0;
public:
    uint32_t irq_flags() {
#if RADIOLIB_VERSION_MAJOR >= 7
        return hardware_radio().getIrqFlags();
#else
        return hardware_radio().getIrqStatus();
#endif
    }
    bool start_rx() {
        radio_irq = false;
        receiving_ = hardware_radio().startReceive() == RADIOLIB_ERR_NONE;
        rx_preamble_at_ = 0;
        if (!receiving_) status("MeshCore RX start failed");
        return receiving_;
    }
    void begin() override { hardware_radio().setDio1Action(radio_isr); start_rx(); }
    bool isInRecvMode() const override { return receiving_; }
    bool isReceiving() override {
        if (!receiving_ || transmitting_) return false;
        if (radio_irq) return true;
        const auto flags = irq_flags();
        if (flags & (RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID)) {
            if (!rx_preamble_at_) rx_preamble_at_ = millis();
            // A false preamble must not permanently block all queued packets.
            if ((uint32_t)(millis() - rx_preamble_at_) < getEstAirtimeFor(MAX_TRANS_UNIT) + 1000) return true;
            hardware_radio().standby(); start_rx();
        }
        return false;
    }
    int recvRaw(uint8_t *bytes, int capacity) override {
        if (transmitting_) return 0;
        if (!radio_irq) { if (!receiving_) start_rx(); return 0; }
        radio_irq = false;
        int length = 0;
        const auto flags = irq_flags();
        if ((flags & RADIOLIB_SX126X_IRQ_RX_DONE) && !(flags & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR))) {
            const size_t received = hardware_radio().getPacketLength();
            // Discard oversize frames; never pass truncated ciphertext upstream.
            if (received > 0 && received <= (size_t)capacity && received <= MAX_TRANS_UNIT &&
                hardware_radio().readData(bytes, received) == RADIOLIB_ERR_NONE) {
                length = valid_radio_frame(bytes, received) ? (int)received : 0;
                rssi_ = hardware_radio().getRSSI(); snr_ = hardware_radio().getSNR();
            }
        }
        hardware_radio().standby();
        start_rx(); // restore reception BEFORE parsing crypto and delivering callbacks
        return length;
    }
    uint32_t getEstAirtimeFor(int bytes) override { return (hardware_radio().getTimeOnAir(bytes) + 999) / 1000; }
    float packetScore(float snr, int) override { return (snr + 20.0f) / 30.0f; }
    bool startSendRaw(const uint8_t *bytes, int length) override {
        if (transmitting_ || length <= 0 || length > MAX_TRANS_UNIT || radio_irq || isReceiving()) return false;
        hardware_radio().standby(); receiving_ = false; radio_irq = false; finished_ = false;
        transmitting_ = hardware_radio().startTransmit(const_cast<uint8_t *>(bytes), length) == RADIOLIB_ERR_NONE;
        if (!transmitting_) start_rx();
        return transmitting_;
    }
    bool isSendComplete() override {
        if (!transmitting_) return false;
        const auto flags = irq_flags(); // polling also recovers a lost DIO interrupt
        if (!(flags & RADIOLIB_SX126X_IRQ_TX_DONE) || (flags & RADIOLIB_SX126X_IRQ_TIMEOUT)) return false;
        const int state = hardware_radio().finishTransmit();
        transmitting_ = false; finished_ = true;
        start_rx();
        return state == RADIOLIB_ERR_NONE;
    }
    void onSendFinished() override {
        if (!finished_) { hardware_radio().finishTransmit(); transmitting_ = false; start_rx(); }
        finished_ = false;
    }
    void loop() override {
        if (!receiving_ && !transmitting_) start_rx();
        if (receiving_ && !transmitting_ &&
            (irq_flags() & (RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_TIMEOUT | RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR))) radio_irq = true;
    }
    float getLastRSSI() const override { return rssi_; }
    float getLastSNR() const override { return snr_; }
} radio;

struct Delivery {
    bool text = false, position = false, notify = false;
    char sender[40] = {}, body[201] = {}, key[65] = {};
    double lat = 0, lon = 0;
    uint32_t from = 0, to = BROADCAST, id = 0;
    uint8_t slot = 0;
    int32_t rssi = 0;
    float snr = 0;
    meshtastic_service_text_rx_cb_t callback = nullptr;
};
constexpr unsigned DELIVERY_CAPACITY = 6;
Delivery deliveries[DELIVERY_CAPACITY];
unsigned delivery_count = 0;
void queue_delivery(const Delivery &d) {
    if (delivery_count < DELIVERY_CAPACITY) deliveries[delivery_count++] = d;
    else status("MeshCore notification queue full");
}
void deliver(const Delivery &d) {
    if (d.notify) {
        StaticJsonDocument<512> document;
        document["t"] = "notify"; document["src"] = "MeshCore";
        document["title"] = d.sender; document["body"] = d.body;
        char json[512]; const size_t n = serializeJson(document, json, sizeof(json));
        if (n && n < sizeof(json)) bluetooth_message_queue_msg(json);
    }
    if (d.text) {
        xnode_send_meshtastic_rx(d.sender, d.body);
        if (d.callback) d.callback(d.from, d.to, d.slot, d.id, d.rssi, d.snr, d.body);
    }
    if (d.position) {
        // The map's legacy marker interface is display-only; full Ed25519 keys
        // stay in BaseChatMesh contacts and are never truncated for routing.
        osmmap_set_external_marker(d.lon, d.lat, d.sender);
        xnode_send_peer_location(d.lat, d.lon, d.sender, d.key);
    }
}
mesh_message_t record(const char *sender, const char *text, uint8_t slot, uint32_t from, uint32_t to, uint32_t id, bool outgoing) {
    mesh_message_t m = {};
    m.from_node = from; m.to_node = to; m.channel_slot = slot; m.packet_id = id;
    m.timestamp = unix_time(); m.uptime_ms = millis(); m.outgoing = outgoing;
    m.status = outgoing ? MESH_MESSAGE_QUEUED : MESH_MESSAGE_RECEIVED;
    strlcpy(m.sender, sender, sizeof(m.sender)); strlcpy(m.text, text, sizeof(m.text));
    return m;
}
MeshCorePacketPool pool;
SimpleMeshTables tables;
struct Pending { mesh::Packet *packet = nullptr; uint32_t sequence = 0; };
class Chat : public BaseChatMesh {
    Pending pending_[MeshCorePacketPool::CAPACITY];
    mesh::Packet *captured_ = nullptr;
    void completion(mesh::Packet *packet, bool ok) {
        for (auto &pending : pending_) if (pending.packet == packet) {
            history.set_status(pending.sequence, ok ? MESH_MESSAGE_TRANSMITTED : MESH_MESSAGE_FAILED);
            pending.packet = nullptr; status(ok ? "MeshCore transmitted (unacknowledged)" : "MeshCore TX failed"); return;
        }
    }
    void incoming(const char *sender, const char *body, uint32_t from, uint32_t to, uint8_t slot, mesh::Packet *packet) {
        uint8_t hash[MAX_HASH_SIZE]; packet->calculatePacketHash(hash);
        uint32_t id; memcpy(&id, hash, sizeof(id)); // UI correlation only, never recipient identity
        history.add(record(sender, body, slot, from, to, id, false));
        strlcpy(last_sender, sender, sizeof(last_sender)); strlcpy(last_text, body, sizeof(last_text));
        last_peer = from; last_rssi = (int32_t)lroundf(radio.getLastRSSI()); last_snr = radio.getLastSNR();
        Delivery d; d.text = d.notify = true; d.from = from; d.to = to; d.id = id; d.slot = slot;
        d.rssi = last_rssi; d.snr = last_snr; d.callback = text_callback;
        strlcpy(d.sender, sender, sizeof(d.sender)); strlcpy(d.body, body, sizeof(d.body)); queue_delivery(d);
        status("MeshCore RX %s", sender);
    }
protected:
    bool allowPacketForward(const mesh::Packet *) override { return false; } // companion/client, not repeater
    bool shouldOverwriteWhenFull() const override { return true; }
    int calcRxDelay(float, uint32_t) const override { return 0; }
    float getAirtimeBudgetFactor() const override { return 1.0f; } // upstream companion default
    uint32_t getCADFailMaxDuration() const override { return radio.getEstAirtimeFor(MAX_TRANS_UNIT) + 1500; }
    void logTx(mesh::Packet *packet, int) override { completion(packet, true); }
    void logTxFail(mesh::Packet *packet, int) override { completion(packet, false); }
    void sendFloodScoped(const mesh::GroupChannel &channel, mesh::Packet *packet, uint32_t delay) override {
        captured_ = packet; BaseChatMesh::sendFloodScoped(channel, packet, delay);
    }
    void onDiscoveredContact(ContactInfo &, bool, uint8_t, const uint8_t *) override {}
    void onContactPathUpdated(const ContactInfo &) override {}
    ContactInfo *processAck(const uint8_t *) override { return nullptr; } // group broadcasts have NO acknowledgements
    void onSendTimeout() override {}
    uint32_t calcFloodTimeoutMillisFor(uint32_t airtime) const override { return airtime * 16 + 5000; }
    uint32_t calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t hops) const override { return airtime * (hops + 2) + 3000; }
    void onMessageRecv(const ContactInfo &contact, mesh::Packet *packet, uint32_t, const char *text) override {
        // Receiving a real upstream direct packet is supported, but the old UI
        // cannot address a 32-byte identity. It may display it with from=0 only.
        incoming(contact.name, text, 0, LOCAL_HANDLE, config.active_slot, packet);
    }
    void onCommandDataRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *) override {}
    void onSignedMessageRecv(const ContactInfo &contact, mesh::Packet *packet, uint32_t, const uint8_t *, const char *text) override {
        incoming(contact.name, text, 0, LOCAL_HANDLE, config.active_slot, packet);
    }
    uint8_t onContactRequest(const ContactInfo &, uint32_t, const uint8_t *, uint8_t, uint8_t *) override { return 0; }
    void onContactResponse(const ContactInfo &, const uint8_t *, uint8_t) override {}
    int searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel *dest, int max_matches) override {
        int count = 0;
        for (unsigned slot = 0; slot < CHANNEL_COUNT && count < max_matches; ++slot) {
            mesh::GroupChannel c;
            if (channel(slot, c) && c.hash[0] == hash[0]) dest[count++] = c;
        }
        return count;
    }
    void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *packet, uint32_t, const char *text) override {
        for (uint8_t slot = 0; slot < CHANNEL_COUNT; ++slot) {
            mesh::GroupChannel c;
            if (this->channel(slot, c) && memcmp(c.secret, channel.secret, sizeof(c.secret)) == 0 && c.hash[0] == channel.hash[0]) {
                char sender[40], body[201]; split_group_text(text, sender, sizeof(sender), body, sizeof(body));
                incoming(sender, body, 0, BROADCAST, slot, packet); return;
            }
        }
    }
    void onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp, const uint8_t *data, size_t length) override {
        // Preserve BaseChatMesh's replay protection for map delivery as well.
        const ContactInfo *previous = lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE);
        if (previous && timestamp <= previous->last_advert_timestamp) return;
        AdvertDataParser parser(data, length);
        if (!parser.isValid() || !parser.hasName()) return;
        BaseChatMesh::onAdvertRecv(packet, id, timestamp, data, length);
        if (!parser.hasLatLon() || !valid_location(parser.getLat(), parser.getLon())) return;
        Delivery d; d.position = true; d.lat = parser.getLat(); d.lon = parser.getLon();
        hex_key(id.pub_key, d.key);
        strlcpy(d.sender, parser.hasName() ? parser.getName() : "MeshCore peer", sizeof(d.sender));
        queue_delivery(d);
    }
public:
    Chat() : BaseChatMesh(radio, clock_source, random_source, clock_source, pool, tables) {}
    bool channel(uint8_t slot, mesh::GroupChannel &out) {
        if (slot >= CHANNEL_COUNT || !config.channels[slot].enabled) return false;
        const auto &c = config.channels[slot]; memset(&out, 0, sizeof(out));
        memcpy(out.secret, c.psk, c.psk_len); mesh::Utils::sha256(out.hash, sizeof(out.hash), c.psk, c.psk_len); return true;
    }
    bool send_group(const char *text, uint8_t slot) {
        mesh::GroupChannel c;
        if (!channel(slot, c)) { status("MeshCore channel unavailable"); return false; }
        if (!valid_group_text(config.name, text)) { status("MeshCore text limit: %u bytes", (unsigned)(GROUP_TEXT_BYTES - strlen(config.name) - 2)); return false; }
        if (!unix_time()) { status("Set clock before MeshCore transmission"); return false; }
        Pending *pending = nullptr;
        for (auto &p : pending_) if (!p.packet) { pending = &p; break; }
        if (!pending) { status("MeshCore TX queue full"); return false; }
        captured_ = nullptr;
        if (!sendGroupMessage(clock_source.getCurrentTimeUnique(), c, config.name, text, strlen(text)) || !captured_) {
            status("MeshCore packet pool full"); return false;
        }
        uint8_t hash[MAX_HASH_SIZE]; captured_->calculatePacketHash(hash); uint32_t id; memcpy(&id, hash, sizeof(id));
        pending->packet = captured_; pending->sequence = history.add(record("Me", text, slot, LOCAL_HANDLE, BROADCAST, id, true));
        strlcpy(last_sender, "Me", sizeof(last_sender)); strlcpy(last_text, text, sizeof(last_text));
        status("MeshCore queued"); return true;
    }
} *chat = nullptr;

bool load_identity(Chat &app) {
    Preferences prefs;
    if (!prefs.begin("meshcore", false)) { status("MeshCore identity storage unavailable"); return false; }
    uint8_t bytes[PUB_KEY_SIZE + PRV_KEY_SIZE] = {};
    const bool exists = prefs.isKey("id_v1");
    if (exists) {
        if (prefs.getBytesLength("id_v1") != sizeof(bytes) || prefs.getBytes("id_v1", bytes, sizeof(bytes)) != sizeof(bytes)) {
            prefs.end(); status("MeshCore identity unreadable; retained"); return false;
        }
        app.self_id.readFrom(bytes, sizeof(bytes));
    } else {
        // With Wi-Fi/BLE disabled, esp_random alone is not guaranteed true
        // entropy. Mix in SX1262 wideband radio noise while the radio is idle.
        class IdentityRandom : public mesh::RNG {
            void random(uint8_t *out, size_t count) override {
                esp_fill_random(out, count);
                for (size_t i = 0; i < count; ++i) out[i] ^= hardware_radio().randomByte();
            }
        } identity_random;
        for (unsigned attempt = 0; attempt < 10; ++attempt) {
            app.self_id = mesh::LocalIdentity(&identity_random);
            if (app.self_id.pub_key[0] != 0 && app.self_id.pub_key[0] != 0xff) break;
        }
        if (app.self_id.pub_key[0] == 0 || app.self_id.pub_key[0] == 0xff) {
            prefs.end(); status("MeshCore identity entropy failed"); return false;
        }
        if (app.self_id.writeTo(bytes, sizeof(bytes)) != sizeof(bytes) || prefs.putBytes("id_v1", bytes, sizeof(bytes)) != sizeof(bytes)) {
            prefs.end(); memset(bytes, 0, sizeof(bytes)); status("MeshCore identity save failed"); return false;
        }
        uint8_t verify[sizeof(bytes)] = {};
        const bool saved = prefs.getBytes("id_v1", verify, sizeof(verify)) == sizeof(verify) && memcmp(bytes, verify, sizeof(bytes)) == 0;
        memset(verify, 0, sizeof(verify));
        if (!saved) { prefs.end(); memset(bytes, 0, sizeof(bytes)); status("MeshCore identity verify failed"); return false; }
    }
    prefs.end();
    // Detect a mismatched/corrupt public/private pair without replacing it.
    static const uint8_t challenge[] = "XNODE identity integrity";
    uint8_t signature[SIGNATURE_SIZE]; app.self_id.sign(signature, challenge, sizeof(challenge));
    const bool valid = app.self_id.verify(signature, challenge, sizeof(challenge));
    memset(bytes, 0, sizeof(bytes)); memset(signature, 0, sizeof(signature));
    if (!valid) { status("MeshCore identity invalid; retained for recovery"); return false; }
    hex_key(app.self_id.pub_key, public_hex); return true;
}
bool event_callback(EventBits_t event, void *) {
    Lock lock;
    if (event == POWERMGM_STANDBY && ready) return false; // display off, radio/MCU still servicing IRQs
    return true;
}
bool loop_callback(EventBits_t, void *) {
    // Copy bounded delivery batch out under lock. BLE/UI may re-enter service,
    // therefore no external callbacks run with the radio lock held.
    // The power-manager loop has one caller; keep this ~2 KB batch off its stack.
    static Delivery batch[DELIVERY_CAPACITY]; unsigned count = 0;
    {
        Lock lock;
        if (!ready || !chat) return true;
        chat->loop();
        if (advert_due && time_due(millis(), advert_at)) {
            if (meshcore_service_broadcast_node_info()) advert_at = millis() + ADVERT_INTERVAL;
            else advert_at = millis() + 5000;
        }
        count = delivery_count;
        for (unsigned i = 0; i < count; ++i) batch[i] = deliveries[i];
        delivery_count = 0;
    }
    for (unsigned i = 0; i < count; ++i) deliver(batch[i]);
    return true;
}
} // namespace

void meshcore_service_setup() {
    Lock lock;
    if (started) return;
    started = true;
    if (!mutex) { status("MeshCore mutex unavailable"); return; }
    if (!config_loaded) return;
    active_radio = config.radio;
    auto &hw = hardware_radio();
    int error = hw.begin(active_radio.frequency_mhz, active_radio.bandwidth_khz, active_radio.spreading_factor,
        active_radio.coding_rate, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, active_radio.tx_power_dbm,
        active_radio.spreading_factor <= 8 ? 32 : 16, TCXO_VOLTS, false);
    if (error == RADIOLIB_ERR_NONE) error = hw.setCurrentLimit(140.0f);
    if (error == RADIOLIB_ERR_NONE) error = hw.setDio2AsRfSwitch(true);
    // Nonzero enables LoRa CRC in both bundled RadioLib 6 and 7 APIs.
    if (error == RADIOLIB_ERR_NONE) error = hw.setCRC(true);
    if (error != RADIOLIB_ERR_NONE) { status("MeshCore radio init failed %d", error); return; }
    static Chat app; chat = &app;
    if (!load_identity(app)) { hw.sleep(); return; }
    app.begin();
    ready = radio.isInRecvMode();
    if (!ready) return;
    powermgm_register_cb(POWERMGM_STANDBY | POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP, event_callback, "meshcore event");
    powermgm_register_loop_cb(POWERMGM_STANDBY | POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP, loop_callback, "meshcore loop");
    status("MeshCore ready (US/CAN preset)");
    advert_due = true; advert_at = millis() + 5000;
}
bool meshcore_service_send_text(const char *text) { Lock lock; return meshcore_service_send_text_to(text, BROADCAST, config.active_slot); }
bool meshcore_service_send_text_to(const char *text, uint32_t destination, uint8_t slot) {
    Lock lock;
    if (destination != BROADCAST) { status("MeshCore DM needs full public key; unsupported"); return false; }
    if (!ready || !chat) { status("MeshCore radio unavailable"); return false; }
    return chat->send_group(text, slot);
}
bool meshcore_service_is_ready() { Lock lock; return ready; }
bool meshcore_service_is_receiving() { Lock lock; return ready && radio.isInRecvMode(); }
const char *meshcore_service_get_status() { return status_text; }
uint8_t meshcore_service_get_channel_count() { Lock lock; uint8_t n = 0; for (auto &c : config.channels) if (c.enabled) ++n; return n; }
int8_t meshcore_service_get_channel_slot(uint8_t index) { Lock lock; for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) if (config.channels[i].enabled && index-- == 0) return i; return -1; }
uint32_t meshcore_service_get_history_revision() { Lock lock; return history.revision(); }
size_t meshcore_service_get_history_count(uint8_t slot) { Lock lock; return history.count(slot); }
bool meshcore_service_get_history_message(uint8_t slot, size_t index, mesh_message_t *out) { Lock lock; return history.get(slot, index, out); }
const char *meshcore_service_get_channel_name(uint8_t index) { Lock lock; int slot = meshcore_service_get_channel_slot(index); return slot < 0 ? "" : config.channels[slot].name; }
uint8_t meshcore_service_get_active_channel() { Lock lock; uint8_t index = 0; for (uint8_t i = 0; i < config.active_slot; ++i) if (config.channels[i].enabled) ++index; return index; }
bool meshcore_service_set_active_channel(uint8_t index) {
    Lock lock; int slot = meshcore_service_get_channel_slot(index); if (!config_loaded || slot < 0) return false;
    Config next = config; next.active_slot = slot;
    if (!persist(next)) { status("MeshCore config save failed"); return false; } config = next; return true;
}
const char *meshcore_service_get_active_channel_name() { Lock lock; return config.channels[config.active_slot].name; }
const char *meshcore_service_get_primary_channel_name() { Lock lock; return config.channels[0].name; }
float meshcore_service_get_frequency_mhz() { Lock lock; return active_radio.frequency_mhz; }
uint32_t meshcore_service_get_node_id() { return public_hex[0] ? LOCAL_HANDLE : 0; }
const char *meshcore_service_get_long_name() { Lock lock; return config.name; }
const char *meshcore_service_get_short_name() { Lock lock; return config.short_name; }
const char *meshcore_service_get_public_key_hex() { return public_hex; }
bool meshcore_service_get_user_info(meshtastic_service_user_info_t *out) {
    Lock lock; if (!out) return false; memset(out, 0, sizeof(*out));
    strlcpy(out->long_name, config.name, sizeof(out->long_name)); strlcpy(out->short_name, config.short_name, sizeof(out->short_name)); return true;
}
bool meshcore_service_set_user_info(const meshtastic_service_user_info_t *info) {
    Lock lock;
    if (!config_loaded || !info || info->is_licensed || info->is_unmessageable ||
        !valid_name(info->long_name, sizeof(info->long_name)) || strlen(info->long_name) > MAX_NAME ||
        strnlen(info->short_name, sizeof(info->short_name)) >= sizeof(info->short_name)) return false;
    Config next = config; strlcpy(next.name, info->long_name, sizeof(next.name)); strlcpy(next.short_name, info->short_name, sizeof(next.short_name));
    if (!persist(next)) { status("MeshCore config save failed"); return false; }
    config = next; meshcore_service_schedule_node_info_broadcast(1000); return true;
}
bool meshcore_service_broadcast_node_info() {
    Lock lock;
    if (!ready || !chat || !unix_time()) return false;
    auto packet = chat->createSelfAdvert(config.name);
    if (!packet) return false;
    chat->sendFlood(packet); return true;
}
void meshcore_service_schedule_node_info_broadcast(uint32_t delay) { Lock lock; advert_due = true; advert_at = millis() + (delay > INT32_MAX ? INT32_MAX : delay); }
uint32_t meshcore_service_get_last_peer() { Lock lock; return last_peer; }
int32_t meshcore_service_get_last_rssi() { Lock lock; return last_rssi; }
float meshcore_service_get_last_snr() { Lock lock; return last_snr; }
const char *meshcore_service_get_last_message_sender() { return last_sender; }
const char *meshcore_service_get_last_message_text() { return last_text; }
bool meshcore_service_get_channel_info(uint8_t slot, meshtastic_service_channel_info_t *out) { Lock lock; if (!out || slot >= CHANNEL_COUNT) return false; *out = config.channels[slot]; return true; }
bool meshcore_service_set_channel_info(uint8_t slot, const meshtastic_service_channel_info_t *info) {
    Lock lock; if (!config_loaded || !info || !channel_valid(slot, *info)) return false;
    Config next = config; next.channels[slot] = *info;
    if (!next.channels[next.active_slot].enabled) next.active_slot = 0;
    if (!persist(next)) { status("MeshCore config save failed"); return false; } config = next; return true;
}
void meshcore_service_set_text_rx_callback(meshtastic_service_text_rx_cb_t callback) { Lock lock; text_callback = callback; }
bool meshcore_service_get_radio_config(meshcore_service_radio_config_t *out) { Lock lock; if (!out) return false; *out = config.radio; return true; }
bool meshcore_service_get_active_radio_config(meshcore_service_radio_config_t *out) { Lock lock; if (!out) return false; *out = active_radio; return true; }
bool meshcore_service_radio_reboot_required() { Lock lock; return !radio_same(config.radio, active_radio); }
bool meshcore_service_set_radio_config(const meshcore_service_radio_config_t *r) {
    Lock lock; if (!config_loaded || !r || !radio_valid(*r)) return false;
    Config next = config; next.radio = *r;
    if (!persist(next)) { status("MeshCore radio config save failed"); return false; }
    config = next; status(meshcore_service_radio_reboot_required() ? "MeshCore radio saved; reboot required" : "MeshCore radio unchanged"); return true;
}

#else
// The legacy facade is linked on unsupported boards/native builds too. These
// stubs expose no fabricated radio, identity, messages or enabled channels.
void meshcore_service_setup() {}
bool meshcore_service_send_text(const char *) { return false; }
bool meshcore_service_send_text_to(const char *, uint32_t, uint8_t) { return false; }
bool meshcore_service_is_ready() { return false; }
bool meshcore_service_is_receiving() { return false; }
const char *meshcore_service_get_status() { return "MeshCore unavailable on this target"; }
uint8_t meshcore_service_get_channel_count() { return 0; }
int8_t meshcore_service_get_channel_slot(uint8_t) { return -1; }
uint32_t meshcore_service_get_history_revision() { return 0; }
size_t meshcore_service_get_history_count(uint8_t) { return 0; }
bool meshcore_service_get_history_message(uint8_t, size_t, mesh_message_t *) { return false; }
const char *meshcore_service_get_channel_name(uint8_t) { return ""; }
uint8_t meshcore_service_get_active_channel() { return 0; }
bool meshcore_service_set_active_channel(uint8_t) { return false; }
const char *meshcore_service_get_active_channel_name() { return ""; }
const char *meshcore_service_get_primary_channel_name() { return ""; }
float meshcore_service_get_frequency_mhz() { return 0; }
uint32_t meshcore_service_get_node_id() { return 0; }
const char *meshcore_service_get_long_name() { return ""; }
const char *meshcore_service_get_short_name() { return ""; }
bool meshcore_service_get_user_info(meshtastic_service_user_info_t *) { return false; }
bool meshcore_service_set_user_info(const meshtastic_service_user_info_t *) { return false; }
bool meshcore_service_broadcast_node_info() { return false; }
void meshcore_service_schedule_node_info_broadcast(uint32_t) {}
uint32_t meshcore_service_get_last_peer() { return 0; }
int32_t meshcore_service_get_last_rssi() { return 0; }
float meshcore_service_get_last_snr() { return 0; }
const char *meshcore_service_get_last_message_sender() { return ""; }
const char *meshcore_service_get_last_message_text() { return ""; }
bool meshcore_service_get_channel_info(uint8_t, meshtastic_service_channel_info_t *) { return false; }
bool meshcore_service_set_channel_info(uint8_t, const meshtastic_service_channel_info_t *) { return false; }
void meshcore_service_set_text_rx_callback(meshtastic_service_text_rx_cb_t) {}
const char *meshcore_service_get_public_key_hex() { return ""; }
bool meshcore_service_get_radio_config(meshcore_service_radio_config_t *) { return false; }
bool meshcore_service_get_active_radio_config(meshcore_service_radio_config_t *) { return false; }
bool meshcore_service_radio_reboot_required() { return false; }
bool meshcore_service_set_radio_config(const meshcore_service_radio_config_t *) { return false; }
#endif
