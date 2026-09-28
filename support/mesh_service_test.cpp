// Execute the production TX and power-loop function bodies with a controlled
// radio. This checks bookkeeping/error handling, not an over-the-air exchange.
#include "app/meshtastic/mesh_history.h"
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <mutex>
#include <thread>

using EventBits_t = uint32_t;
constexpr int RADIOLIB_ERR_NONE = 0;
constexpr uint16_t RADIOLIB_SX126X_IRQ_TX_DONE = 1, RADIOLIB_SX126X_IRQ_TIMEOUT = 512;
constexpr uint32_t POWERMGM_STANDBY = 1, POWERMGM_WAKEUP = 2, POWERMGM_SILENCE_WAKEUP = 4;
constexpr size_t MESHTASTIC_MAX_PACKET_LEN = 255, MESHTASTIC_MAX_TEXT_LEN = 200;
constexpr uint8_t MESHTASTIC_CHANNEL_COUNT = 8, MESHTASTIC_HOP_RELIABLE = 3;
constexpr uint8_t MESHTASTIC_FLAG_HOP_START_SHIFT = 5, MESHTASTIC_FLAG_HOP_START_MASK = 0xe0;
constexpr uint32_t MESHTASTIC_TEXT_MESSAGE_APP = 1, MESHTASTIC_NODEINFO_INTERVAL_MS = 900000;
constexpr uint32_t MESHTASTIC_NODEINFO_RETRY_DELAY_MS = 5000;
struct meshtastic_packet_header_t { uint32_t to, from, id; uint8_t flags, channel, next_hop, relay_node; };
struct meshtastic_runtime_channel_t { bool enabled; char name[16]; uint8_t hash, psk[32]; size_t psk_len; };
meshtastic_runtime_channel_t meshtastic_channels[8] = {};
struct Radio {
    int start_result = 0, finish_result = 0, starts = 0;
    uint16_t irq = 0;
    int startTransmit(uint8_t *, size_t) { ++starts; return start_result; }
    int finishTransmit() { return finish_result; }
    #if RADIOLIB_VERSION_MAJOR >= 7
        uint32_t getIrqFlags() { return irq; }
    #else
        uint16_t getIrqStatus() { return irq; }
    #endif
    void sleep() {}
    void standby() {}
} meshtastic_radio;
std::recursive_mutex radio_mutex;
struct MeshtasticRadioLock { MeshtasticRadioLock() {radio_mutex.lock();} ~MeshtasticRadioLock() {radio_mutex.unlock();} };
MeshHistory history;
bool meshtastic_radio_ready = true, meshtastic_tx_active = false, meshtastic_radio_receiving = true;
bool meshtastic_radio_irq = false, meshtastic_nodeinfo_due = false;
uint32_t meshtastic_pending_sequence = 0, meshtastic_tx_started_ms = 0, meshtastic_nodeinfo_due_ms = 0;
uint32_t meshtastic_node_id = 42, fake_now = 100;
char meshtastic_pending_text[201] = {}, meshtastic_pending_channel_name[16] = {}, service_status[96] = {};
unsigned notifications = 0;
uint32_t millis() {return fake_now;}
size_t strlcpy(char *out, const char *in, size_t n) {size_t len=strlen(in); if(n){size_t copy=len<n-1?len:n-1;memcpy(out,in,copy);out[copy]=0;}return len;}
uint32_t mesh_history_add(const mesh_message_t &message) {return history.add(message);}
void mesh_history_status(uint32_t sequence, mesh_message_status_t status) {history.set_status(sequence,status);}
mesh_message_t meshtastic_history_record(const char *sender,const char *text,uint32_t from,uint32_t to,uint8_t slot,uint32_t id,bool outgoing) {
    mesh_message_t r={};r.from_node=from;r.to_node=to;r.channel_slot=slot;r.packet_id=id;r.outgoing=outgoing;
    r.status=outgoing?MESH_MESSAGE_QUEUED:MESH_MESSAGE_RECEIVED;r.uptime_ms=millis();
    strlcpy(r.sender,sender,sizeof(r.sender));strlcpy(r.text,text,sizeof(r.text));return r;
}
void meshtastic_update_status(const char *format,...) {va_list args;va_start(args,format);vsnprintf(service_status,sizeof(service_status),format,args);va_end(args);}
size_t meshtastic_encode_data_message(uint8_t*,size_t,uint32_t,const uint8_t*,size_t n,uint32_t,uint32_t) {return n;}
void meshtastic_crypt_payload(uint32_t,uint32_t,uint8_t*,size_t,const uint8_t*,size_t) {}
uint32_t meshtastic_generate_packet_id() {static uint32_t id=500;return ++id;}
bool meshtastic_time_due(uint32_t now,uint32_t due) {return (int32_t)(now-due)>=0;}
bool meshtastic_service_broadcast_node_info() {return false;}
bool meshtastic_start_receive() {meshtastic_tx_active=false;meshtastic_radio_receiving=true;return true;}
void meshtastic_store_last_message(const char*,const char*) {}
void meshtastic_queue_notification(const char*,const char*) {++notifications;}
void meshtastic_handle_rx() {}

#include "mesh_service_actual.inc"

static mesh_message_status_t latest() {
    mesh_message_t out={};assert(history.get(3,history.count(3)-1,&out));return out.status;
}
static void complete(uint16_t irq,int finish=0) {
    meshtastic_radio.irq=irq;meshtastic_radio.finish_result=finish;meshtastic_radio_irq=true;
    meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
}
int main() {
    meshtastic_channels[3].enabled=true;strcpy(meshtastic_channels[3].name,"Field");
    assert(!meshtastic_service_send_text_internal("",0xffffffff,3));
    char too_long[202];memset(too_long,'x',201);too_long[201]=0;
    assert(!meshtastic_service_send_text_internal(too_long,0xffffffff,3));
    assert(history.count(3)==0 && meshtastic_radio.starts==0);
    meshtastic_radio_ready=false;
    assert(!meshtastic_service_send_text_internal("Offline",0xffffffff,3));
    assert(latest()==MESH_MESSAGE_FAILED && meshtastic_radio.starts==0);
    meshtastic_radio_ready=true;
    meshtastic_radio.start_result=-1;
    assert(!meshtastic_service_send_text_internal("Start fails",0xffffffff,3));
    assert(latest()==MESH_MESSAGE_FAILED && !meshtastic_tx_active);
    meshtastic_radio.start_result=0;
    meshtastic_radio_irq=true; // Stale RX flag cannot complete a newly queued send.
    assert(meshtastic_service_send_text_internal("Queued",0xffffffff,3));
    assert(latest()==MESH_MESSAGE_QUEUED && !meshtastic_radio_irq);
    const uint32_t accepted=meshtastic_pending_sequence;
    assert(!meshtastic_service_send_text_internal("Busy",0xffffffff,3));
    assert(latest()==MESH_MESSAGE_FAILED && meshtastic_pending_sequence==accepted);
    complete(RADIOLIB_SX126X_IRQ_TX_DONE);
    mesh_message_t out={};assert(history.get(3,2,&out));assert(out.sequence==accepted && out.status==MESH_MESSAGE_TRANSMITTED);
    assert(notifications==1);
    assert(meshtastic_service_send_text_internal("Timeout IRQ",0xffffffff,3));
    complete(RADIOLIB_SX126X_IRQ_TIMEOUT);assert(latest()==MESH_MESSAGE_FAILED && notifications==1);
    assert(meshtastic_service_send_text_internal("Cleanup error",0xffffffff,3));
    complete(RADIOLIB_SX126X_IRQ_TX_DONE,-1);assert(latest()==MESH_MESSAGE_FAILED);
    assert(meshtastic_service_send_text_internal("Missing interrupt",0xffffffff,3));
    fake_now+=30000;meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(latest()==MESH_MESSAGE_FAILED && !meshtastic_tx_active);
    assert(meshtastic_service_send_text_internal("Sleep",0xffffffff,3));
    meshtastic_powermgm_event_cb(POWERMGM_STANDBY,nullptr);assert(latest()==MESH_MESSAGE_FAILED);
    bool first=false,second=false;
    std::thread a([&]{first=meshtastic_service_send_text_internal("Concurrent A",0xffffffff,3);});
    std::thread b([&]{second=meshtastic_service_send_text_internal("Concurrent B",0xffffffff,3);});
    a.join();b.join();assert(first!=second);
    complete(RADIOLIB_SX126X_IRQ_TX_DONE);
    assert(!meshtastic_tx_active);
    puts("Production mesh TX bodies: invalid, offline, start failure, busy, concurrent send, TX_DONE, timeout, cleanup error, lost IRQ and sleep cancellation passed.");
}
