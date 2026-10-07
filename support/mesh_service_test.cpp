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
#include <cmath>

using EventBits_t = uint32_t;
constexpr int RADIOLIB_ERR_NONE = 0;
constexpr uint8_t RADIOLIB_SX126X_LORA_CRC_ON = 1;
constexpr uint16_t RADIOLIB_SX126X_IRQ_TX_DONE = 1, RADIOLIB_SX126X_IRQ_TIMEOUT = 512;
constexpr uint32_t POWERMGM_STANDBY = 1, POWERMGM_WAKEUP = 2, POWERMGM_SILENCE_WAKEUP = 4;
constexpr size_t MESHTASTIC_MAX_PACKET_LEN = 255, MESHTASTIC_MAX_TEXT_LEN = 200;
constexpr uint8_t MESHTASTIC_CHANNEL_COUNT = 8, MESHTASTIC_HOP_RELIABLE = 3;
constexpr uint8_t MESHTASTIC_FLAG_HOP_START_SHIFT = 5, MESHTASTIC_FLAG_HOP_START_MASK = 0xe0;
constexpr uint32_t MESHTASTIC_TEXT_MESSAGE_APP = 1, MESHTASTIC_NODEINFO_INTERVAL_MS = 900000;
constexpr uint32_t MESHTASTIC_NODEINFO_RETRY_DELAY_MS = 5000;
constexpr uint32_t MESHTASTIC_BROADCAST=0xffffffff,MESHTASTIC_NODEINFO_APP=4,MESHTASTIC_POSITION_APP=3;
struct meshtastic_decoded_data_t {bool valid=false;uint32_t portnum=0,dest=0,source=0,request_id=0;size_t payload_len=0;uint8_t payload[255]={};};
struct meshtastic_decoded_text_t {bool valid=false;uint32_t portnum=0,dest=0,source=0;size_t text_len=0;char text[201]={};};
struct meshtastic_decoded_position_t {bool valid=false,has_altitude=false;int32_t latitude_i=0,longitude_i=0,altitude=0;};
struct meshtastic_User {};
struct meshtastic_packet_header_t { uint32_t to, from, id; uint8_t flags, channel, next_hop, relay_node; };
struct meshtastic_runtime_channel_t { bool enabled; char name[16]; uint8_t hash, psk[32]; size_t psk_len; };
meshtastic_runtime_channel_t meshtastic_channels[8] = {};
uint8_t meshtastic_enabled_channel_slots[8]={0,3},meshtastic_enabled_channel_count=2;
unsigned rx_reads=0;
struct Radio {
    int start_result = 0, finish_result = 0, starts = 0;
    int crc_result = 0, crc_value = -1;
    uint16_t irq = 0;
    uint8_t rx_packet[255]={};size_t rx_len=0;int read_result=0;
    size_t getPacketLength() {return rx_len;}
    int readData(uint8_t *out,size_t n) {++rx_reads;memcpy(out,rx_packet,n);return read_result;}
    float getRSSI() {return -75;}
    float getSNR() {return 8;}
    uint8_t last_tx_flags=0;
    int startTransmit(uint8_t *packet, size_t) { last_tx_flags=packet[12];++starts; return start_result; }
    int finishTransmit() { return finish_result; }
    int setCRC(uint8_t value) {crc_value=value;return crc_result;}
    #if RADIOLIB_VERSION_MAJOR >= 7
        uint32_t getIrqFlags() { return irq; }
    #else
        uint16_t getIrqStatus() { return irq; }
    #endif
    void sleep() {}
    void standby() {}
} meshtastic_radio;
std::recursive_mutex radio_mutex;
thread_local unsigned radio_lock_depth=0;
struct MeshtasticRadioLock { MeshtasticRadioLock() {radio_mutex.lock();++radio_lock_depth;} ~MeshtasticRadioLock() {--radio_lock_depth;radio_mutex.unlock();} };
using rx_cb = void (*)(uint32_t,uint32_t,uint8_t,uint32_t,int32_t,float,const char*);
rx_cb meshtastic_text_rx_callback=nullptr;
struct meshtastic_delivery_t {
    bool notify=false,text_rx=false,position=false;
    char sender[24]={},text[201]={};
    uint32_t from=0,to=0,packet_id=0;
    uint8_t channel_slot=0;
    int32_t rssi=0;float snr=0;double lat=0,lon=0;
    rx_cb callback=nullptr;
};
MeshHistory history;
bool meshtastic_radio_ready = true, meshtastic_tx_active = false, meshtastic_radio_receiving = true;
bool meshtastic_radio_irq = false, meshtastic_nodeinfo_due = false;
uint32_t meshtastic_pending_sequence = 0, meshtastic_tx_started_ms = 0, meshtastic_nodeinfo_due_ms = 0;
uint32_t meshtastic_node_id = 42, fake_now = 100;
uint32_t meshtastic_last_peer=0;int32_t meshtastic_last_rssi=0;float meshtastic_last_snr=0;
char meshtastic_pending_text[201] = {}, meshtastic_pending_channel_name[16] = {}, service_status[96] = {};
unsigned notifications = 0;
uint32_t millis() {return fake_now;}
size_t strlcpy(char *out, const char *in, size_t n) {size_t len=strlen(in); if(n){size_t copy=len<n-1?len:n-1;memcpy(out,in,copy);out[copy]=0;}return len;}
uint32_t mesh_history_add(const mesh_message_t &message) {return history.add(message);}
bool mesh_history_ack(uint32_t id,uint8_t slot,uint32_t local,uint32_t peer,const char *text=nullptr) {return history.acknowledge(id,slot,local,peer,text);}
void mesh_history_status(uint32_t sequence, mesh_message_status_t status) {history.set_status(sequence,status);}
mesh_message_t meshtastic_history_record(const char *sender,const char *text,uint32_t from,uint32_t to,uint8_t slot,uint32_t id,bool outgoing) {
    mesh_message_t r={};r.from_node=from;r.to_node=to;r.channel_slot=slot;r.packet_id=id;r.outgoing=outgoing;
    r.status=outgoing?MESH_MESSAGE_QUEUED:MESH_MESSAGE_RECEIVED;r.uptime_ms=millis();
    strlcpy(r.sender,sender,sizeof(r.sender));strlcpy(r.text,text,sizeof(r.text));return r;
}
void meshtastic_update_status(const char *format,...) {va_list args;va_start(args,format);vsnprintf(service_status,sizeof(service_status),format,args);va_end(args);}
// Only encryption/radio I/O are fixtures; the real protobuf decoder, receive
// handler, history, lifecycle and deferred delivery bodies run below.
void meshtastic_crypt_payload(uint32_t,uint32_t,uint8_t*,size_t,const uint8_t*,size_t) {}
void meshtastic_format_node_label(uint32_t,char *out,size_t n) {strlcpy(out,"FIELD-1",n);}
bool meshtastic_decode_user_payload(const uint8_t*,size_t,meshtastic_User&) {return false;}
void meshtastic_store_peer_user(uint32_t,const meshtastic_User&) {}
bool meshtastic_decode_position_message(const uint8_t*,size_t,meshtastic_decoded_position_t&) {return false;}
uint32_t meshtastic_generate_packet_id() {static uint32_t id=500;return ++id;}
bool meshtastic_time_due(uint32_t now,uint32_t due) {return (int32_t)(now-due)>=0;}
bool meshtastic_service_broadcast_node_info() {return false;}
bool meshtastic_start_receive() {meshtastic_tx_active=false;meshtastic_radio_receiving=true;return true;}
void meshtastic_store_last_message(const char*,const char*) {}
unsigned rx_delivery=0;
void meshtastic_queue_notification(const char*,const char*) {assert(radio_lock_depth==0 && meshtastic_radio_receiving);++notifications;}
void xnode_send_meshtastic_rx(const char*,const char*) {assert(radio_lock_depth==0 && meshtastic_radio_receiving);++rx_delivery;}
void osmmap_set_external_marker(double,double,const char*) {assert(radio_lock_depth==0);}
void xnode_send_peer_location(double,double,const char*) {assert(radio_lock_depth==0);}

#include "mesh_service_actual.inc"

static mesh_message_status_t latest() {
    mesh_message_t out={};assert(history.get(3,history.count(3)-1,&out));return out.status;
}
static void complete(uint16_t irq,int finish=0) {
    meshtastic_radio.irq=irq;meshtastic_radio.finish_result=finish;meshtastic_radio_irq=true;
    meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
}
static void incoming(uint32_t id,uint32_t destination=MESHTASTIC_BROADCAST,uint8_t hash=8) {
    const meshtastic_packet_header_t header={destination,123,id,3,hash,0,0};
    memcpy(meshtastic_radio.rx_packet,&header,sizeof(header));
    // Independent canonical protobuf Data: portnum=TEXT(1), payload="hello".
    const uint8_t body[]={0x08,0x01,0x12,0x05,'h','e','l','l','o'};
    memcpy(meshtastic_radio.rx_packet+sizeof(header),body,sizeof(body));
    meshtastic_radio.rx_len=sizeof(header)+sizeof(body);meshtastic_radio_irq=true;
}
static void ack_packet(uint32_t request,uint32_t peer=123,uint32_t to=42,uint8_t hash=8,uint8_t error=0) {
    const meshtastic_packet_header_t header={to,peer,700,3,hash,0,0};
    memcpy(meshtastic_radio.rx_packet,&header,sizeof(header));
    uint8_t body[]={0x08,5,0x12,2,0x18,error,0x35,0,0,0,0};
    memcpy(body+7,&request,sizeof(request));
    memcpy(meshtastic_radio.rx_packet+sizeof(header),body,sizeof(body));
    meshtastic_radio.rx_len=sizeof(header)+sizeof(body);meshtastic_radio_irq=true;
}
static mesh_message_t channel_last(uint8_t slot) {
    mesh_message_t out={};assert(history.get(slot,history.count(slot)-1,&out));return out;
}
static void ack_tests() {
    const auto delivered=rx_delivery;
    assert(meshtastic_service_send_text_internal("ACK me",MESHTASTIC_BROADCAST,0));
    assert(meshtastic_radio.last_tx_flags & 0x08);
    auto outgoing=channel_last(0);
    complete(RADIOLIB_SX126X_IRQ_TX_DONE);
    assert(channel_last(0).status==MESH_MESSAGE_TRANSMITTED); // Local completion is never ACK.
    ack_packet(outgoing.packet_id+1);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    ack_packet(outgoing.packet_id,123,999);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    ack_packet(outgoing.packet_id,123,42,99);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    ack_packet(outgoing.packet_id,123,42,8,3);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    ack_packet(outgoing.packet_id,42);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_TRANSMITTED);
    ack_packet(outgoing.packet_id);
    meshtastic_radio.rx_packet[meshtastic_radio.rx_len++]=0;
    meshtastic_radio.rx_packet[meshtastic_radio.rx_len++]=0; // Invalid field zero in outer Data.
    meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_TRANSMITTED);
    ack_packet(outgoing.packet_id);
    const size_t payload_at=sizeof(meshtastic_packet_header_t)+4;
    memmove(meshtastic_radio.rx_packet+payload_at+4,meshtastic_radio.rx_packet+payload_at+2,5);
    meshtastic_radio.rx_packet[payload_at-1]=4;
    meshtastic_radio.rx_packet[payload_at+2]=0;
    meshtastic_radio.rx_packet[payload_at+3]=0; // Invalid field zero inside Routing payload.
    meshtastic_radio.rx_len+=2;
    meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_TRANSMITTED);
    ack_packet(outgoing.packet_id);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_ACKNOWLEDGED && rx_delivery==delivered);
    auto revision=history.revision();
    ack_packet(outgoing.packet_id);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(history.revision()==revision); // Duplicate ACK is idempotent.
    assert(meshtastic_service_send_text_internal("Direct",321,0));
    outgoing=channel_last(0);complete(RADIOLIB_SX126X_IRQ_TX_DONE);
    ack_packet(outgoing.packet_id,123);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_TRANSMITTED);
    ack_packet(outgoing.packet_id,321);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_ACKNOWLEDGED);
    assert(meshtastic_service_send_text_internal("hello",MESHTASTIC_BROADCAST,0));
    outgoing=channel_last(0);complete(RADIOLIB_SX126X_IRQ_TX_DONE);
    incoming(outgoing.packet_id);
    meshtastic_packet_header_t echoed={MESHTASTIC_BROADCAST,42,outgoing.packet_id,3,8,0,0};
    memcpy(meshtastic_radio.rx_packet,&echoed,sizeof(echoed));
    meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_TRANSMITTED); // Unchanged own packet is not a relay.
    echoed.flags=2;memcpy(meshtastic_radio.rx_packet,&echoed,sizeof(echoed));meshtastic_radio_irq=true;
    meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_ACKNOWLEDGED && rx_delivery==delivered);
    assert(meshtastic_service_send_text_internal("Lost TX_DONE",MESHTASTIC_BROADCAST,0));
    outgoing=channel_last(0);fake_now+=30000;
    meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_FAILED);
    ack_packet(outgoing.packet_id);meshtastic_powermgm_loop_cb(POWERMGM_WAKEUP,nullptr);
    assert(channel_last(0).status==MESH_MESSAGE_ACKNOWLEDGED);
    assert(!history.set_status(outgoing.sequence,MESH_MESSAGE_FAILED));
    assert(meshtastic_service_send_text_internal("Early ACK",MESHTASTIC_BROADCAST,0));
    outgoing=channel_last(0);ack_packet(outgoing.packet_id);
    meshtastic_delivery_t received_ack;
    { MeshtasticRadioLock lock;assert(meshtastic_handle_rx(received_ack)); }
    assert(channel_last(0).status==MESH_MESSAGE_ACKNOWLEDGED);
    complete(RADIOLIB_SX126X_IRQ_TX_DONE);
    assert(channel_last(0).status==MESH_MESSAGE_ACKNOWLEDGED);
    const uint8_t error_then_none[]={0x18,3,0x18,0};
    const uint8_t discovery[]={0x0a,0};
    const uint8_t malformed[]={0x18,0x80};
    assert(!meshtastic_routing_ack(error_then_none,sizeof(error_then_none)));
    assert(!meshtastic_routing_ack(discovery,sizeof(discovery)));
    assert(!meshtastic_routing_ack(malformed,sizeof(malformed)));
    assert(!meshtastic_routing_ack(nullptr,0));
}
static void malformed_protobuf_tests() {
    const uint8_t max32[]={0xff,0xff,0xff,0xff,0x0f};
    size_t offset=0;uint32_t value=0;
    assert(meshtastic_read_varint(max32,sizeof(max32),offset,value) && value==0xffffffffu);
    const uint8_t overflow[]={0xff,0xff,0xff,0xff,0x10};
    offset=0;assert(!meshtastic_read_varint(overflow,sizeof(overflow),offset,value));
    const uint8_t continued[]={0xff,0xff,0xff,0xff,0x80,0};
    offset=0;assert(!meshtastic_read_varint(continued,sizeof(continued),offset,value));
    const uint8_t truncated[]={0x80};
    offset=0;assert(!meshtastic_read_varint(truncated,sizeof(truncated),offset,value));
    // These lengths wrap offset+length on the embedded 32-bit size_t target.
    const uint8_t huge_payload[]={0x08,1,0x12,0xff,0xff,0xff,0xff,0x0f};
    const uint8_t huge_unknown[]={0x08,1,0x32,0xff,0xff,0xff,0xff,0x0f};
    const uint8_t overflow_payload[]={0x08,1,0x12,0xff,0xff,0xff,0xff,0x10};
    const uint8_t short_fixed[]={0x08,1,0x25,0};
    meshtastic_decoded_data_t decoded={};
    assert(!meshtastic_decode_data_message(huge_payload,sizeof(huge_payload),decoded));
    assert(!meshtastic_decode_data_message(huge_unknown,sizeof(huge_unknown),decoded));
    assert(!meshtastic_decode_data_message(overflow_payload,sizeof(overflow_payload),decoded));
    assert(!meshtastic_decode_data_message(short_fixed,sizeof(short_fixed),decoded));
    offset=2;assert(!meshtastic_skip_field(truncated,sizeof(truncated),offset,1));
    offset=0;assert(!meshtastic_skip_field(truncated,sizeof(truncated),offset,5));
}
int main() {
    malformed_protobuf_tests();
    assert(meshtastic_configure_crc() && meshtastic_radio.crc_value==RADIOLIB_SX126X_LORA_CRC_ON);
    meshtastic_radio.crc_result=-1;assert(!meshtastic_configure_crc());meshtastic_radio.crc_result=0;
    meshtastic_channels[3].enabled=true;strcpy(meshtastic_channels[3].name,"Field");
    meshtastic_channels[0].enabled=true;meshtastic_channels[0].hash=8;strcpy(meshtastic_channels[0].name,"LongFast");
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
    assert(!meshtastic_powermgm_event_cb(POWERMGM_STANDBY,nullptr));
    assert(latest()==MESH_MESSAGE_QUEUED && meshtastic_tx_active);
    complete(RADIOLIB_SX126X_IRQ_TX_DONE);assert(latest()==MESH_MESSAGE_TRANSMITTED);
    assert(!meshtastic_powermgm_event_cb(POWERMGM_STANDBY,nullptr) && meshtastic_radio_receiving);
    incoming(100);
    meshtastic_powermgm_event_cb(POWERMGM_WAKEUP,nullptr);
    assert(meshtastic_radio_irq); // Screen wake must not erase pending radio data.
    meshtastic_powermgm_loop_cb(POWERMGM_STANDBY,nullptr);
    assert(rx_reads==1 && rx_delivery==1 && meshtastic_radio_receiving);
    assert(history.count(0)==1 && history.get(0,0,&out));
    assert(!strcmp(out.text,"hello") && out.from_node==123 && out.channel_slot==0 && out.packet_id==100);
    incoming(100);meshtastic_powermgm_loop_cb(POWERMGM_STANDBY,nullptr);assert(history.count(0)==1);
    const auto delivered=rx_delivery;
    incoming(101,999);meshtastic_powermgm_loop_cb(POWERMGM_STANDBY,nullptr);assert(history.count(0)==1 && rx_delivery==delivered);
    incoming(102,MESHTASTIC_BROADCAST,99);meshtastic_powermgm_loop_cb(POWERMGM_STANDBY,nullptr);assert(rx_delivery==delivered);
    incoming(103);meshtastic_radio.read_result=-7;meshtastic_powermgm_loop_cb(POWERMGM_STANDBY,nullptr);assert(rx_delivery==delivered);meshtastic_radio.read_result=0;
    incoming(104);meshtastic_radio.rx_packet[sizeof(meshtastic_packet_header_t)+3]=200;
    meshtastic_powermgm_loop_cb(POWERMGM_STANDBY,nullptr);assert(rx_delivery==delivered && meshtastic_radio_receiving);
    incoming(105,42);meshtastic_powermgm_loop_cb(POWERMGM_STANDBY,nullptr);assert(history.count(0)==2 && rx_delivery==delivered+1);
    bool first=false,second=false;
    std::thread a([&]{first=meshtastic_service_send_text_internal("Concurrent A",0xffffffff,3);});
    std::thread b([&]{second=meshtastic_service_send_text_internal("Concurrent B",0xffffffff,3);});
    a.join();b.join();assert(first!=second);
    complete(RADIOLIB_SX126X_IRQ_TX_DONE);
    assert(!meshtastic_tx_active);
    ack_tests();
    puts("Production mesh bodies: explicit/implicit ACK correlation and rejection, protobuf RX/header channel and destination routing, corrupt/CRC-rejected RX, dedupe, CRC config/failure, TX concurrency, standby RX/TX and delivery after RX restart/mutex release passed. Radio/crypto are fixtures; no RF claim.");
}
