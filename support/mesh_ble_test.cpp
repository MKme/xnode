#include <cassert>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <ArduinoJson.h>
#include "app/mesh/mesh_protocol.h"
#include "app/meshcore/meshcore_service.h"
#include "hardware/ble/xnode.h"
using SpiRamJsonDocument = DynamicJsonDocument;
#ifdef _WIN32
static size_t strlcpy(char *dst, const char *src, size_t capacity) {
 const size_t length = strlen(src);
 if (capacity) { const size_t count = std::min(length, capacity - 1); memcpy(dst, src, count); dst[count] = 0; }
 return length;
}
#endif
#define HARDWARE_NAME "native-test"
static mesh_protocol_t active=MESH_PROTOCOL_MESHTASTIC, selected=MESH_PROTOCOL_MESHTASTIC;
static bool save_ok=true, ready=true, radio_pending=false;
static unsigned saves=0, sends=0, user_saves=0, broadcasts=0, channel_saves=0;
static uint8_t channel_index=0;
static std::string event_type;
static DynamicJsonDocument event(8192);
static meshtastic_service_user_info_t user={"Fixture", "FX", false, false};
static meshtastic_service_channel_info_t channel={true,1,"Public",16,{}};
static meshcore_service_radio_config_t radio={910.525f,62.5f,7,5,22}, pending_radio=radio;
static uint16_t xnode_watch_unit_id=1, xnode_sos_to_unit_id=2;
static bool xnode_has_location=true;
static double xnode_last_lat=43.1, xnode_last_lon=-80.2;
static unsigned location_persistence_calls=0, event_count=0;
bool xnode_save_persistent_location_if_due(bool){++location_persistence_calls;return true;}
const char *device_get_name(){return "Fixture";}
uint32_t millis(){return 5000;}
mesh_protocol_t mesh_protocol_get_active(){return active;}
mesh_protocol_t mesh_protocol_get_selected(){return selected;}
const char *mesh_protocol_name(mesh_protocol_t p){return p==MESH_PROTOCOL_MESHCORE?"MeshCore":"Meshtastic";}
bool mesh_protocol_is_supported(mesh_protocol_t p){return p==MESH_PROTOCOL_MESHCORE||p==MESH_PROTOCOL_MESHTASTIC;}
bool mesh_protocol_select(mesh_protocol_t p){if(!save_ok)return false;++saves;selected=p;return true;}
bool mesh_protocol_reboot_required(){return selected!=active;}
const char *meshcore_service_get_public_key_hex(){return "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";}
bool meshcore_service_get_radio_config(meshcore_service_radio_config_t *out){*out=pending_radio;return true;}
bool meshcore_service_get_active_radio_config(meshcore_service_radio_config_t *out){*out=radio;return true;}
bool meshcore_service_radio_reboot_required(){return radio_pending;}
bool meshcore_service_set_radio_config(const meshcore_service_radio_config_t *c){
 if(!std::isfinite(c->frequency_mhz)||c->frequency_mhz<902+c->bandwidth_khz/2000||c->frequency_mhz>928-c->bandwidth_khz/2000 || (c->bandwidth_khz!=62.5f&&c->bandwidth_khz!=125&&c->bandwidth_khz!=250))return false;
 if(!save_ok)return false;
 pending_radio=*c;radio_pending=true;return true;
}
bool meshtastic_service_is_ready(){return ready;}
const char *meshtastic_service_get_status(){return ready?"Radio ready":"Radio offline";}
uint32_t meshtastic_service_get_node_id(){return 0xabcdef01;}
const char *meshtastic_service_get_long_name(){return user.long_name;}
bool meshtastic_service_get_user_info(meshtastic_service_user_info_t *out){*out=user;return true;}
bool meshtastic_service_set_user_info(const meshtastic_service_user_info_t *u){if(!save_ok)return false;user=*u;++user_saves;if(active==MESH_PROTOCOL_MESHCORE)++broadcasts;return true;}
void meshtastic_service_schedule_node_info_broadcast(uint32_t){++broadcasts;}
bool meshtastic_service_send_text(const char*){if(!ready)return false;++sends;return true;}
uint8_t meshtastic_service_get_channel_count(){return 2;}
uint8_t meshtastic_service_get_active_channel(){return channel_index;}
int8_t meshtastic_service_get_channel_slot(uint8_t i){return i<2?i*3:-1;}
bool meshtastic_service_set_active_channel(uint8_t i){if(i>1)return false;channel_index=i;return true;}
bool meshtastic_service_get_channel_info(uint8_t,meshtastic_service_channel_info_t *out){*out=channel;return true;}
bool meshtastic_service_set_channel_info(uint8_t,const meshtastic_service_channel_info_t *in){if(!save_ok)return false;channel=*in;++channel_saves;return true;}
bool xnode_send_event(const char *type,JsonVariantConst payload){++event_count;assert(!payload.isNull());event_type=type;event.clear();event.set(payload);assert(!event.overflowed());return true;}
using String=std::string;
using std::min;
static constexpr size_t XNODE_MAX_JSON=6144, XNODE_FRAME_CHUNK=140, XNODE_MAX_ENCODED=8192;
static const char *XNODE_OFFLINE_TILE_ROOT="fixture";
struct xnode_rx_frame_t {char text[256];uint32_t connection_epoch;uint16_t connection_handle;bool authenticated;};
static std::atomic<uint32_t> xnode_connection_epoch{1};
static uint32_t xnode_rx_connection_epoch=0;
static uint16_t xnode_rx_connection_handle=0xffff, xnode_rx_index=0, xnode_rx_total=0;
static bool xnode_rx_authenticated=false;
static char xnode_rx_id[24]={};
static String xnode_rx_encoded;
bool xnode_handle_file_stream_frame(const char*){return true;}
bool xnode_base64url_decode(const char *input,String &output){output=input;return true;}
void xnode_send_status_event(const char*,const char*,const char*){}
struct ble_gap_conn_desc {uint16_t conn_handle;struct {bool encrypted,authenticated;} sec_state;};
class NimBLECharacteristic {public: std::string value;std::string getValue(){return value;}void setValue(const uint8_t*p,size_t n){value.assign((const char*)p,n);}};
class NimBLECharacteristicCallbacks {public: virtual ~NimBLECharacteristicCallbacks(){};virtual void onWrite(NimBLECharacteristic*,ble_gap_conn_desc*){}};
static std::vector<xnode_rx_frame_t> queued_frames;
static void *xnode_rx_queue=(void*)1;
#define pdMS_TO_TICKS(n) (n)
bool xQueueSend(void*,const xnode_rx_frame_t *frame,unsigned){queued_frames.push_back(*frame);return true;}
#include "actual_mesh_ble.inc"
static void command(const char *type,const char *json,bool authenticated=true){DynamicJsonDocument doc(4096);assert(!deserializeJson(doc,json));DynamicJsonDocument envelope(4608);envelope["type"]=type;envelope["payload"]=doc.as<JsonVariantConst>();xnode_handle_command(envelope,authenticated);assert(!event.overflowed());}
static bool ok(){return event["ok"]|false;}
int main(int argc, char **argv){
 if (argc > 1 && std::string(argv[1]) == "--wire") {
  std::string line;
  while (std::getline(std::cin, line)) {
   DynamicJsonDocument envelope(6144);
   if (deserializeJson(envelope, line)) return 2;
   const char *type = envelope["type"] | "";
   if (!strcmp(type, "__restart")) { active=selected; radio=pending_radio; radio_pending=false; xnode_send_hello_ack(); }
   else if (!strcmp(type, "hello")) xnode_send_hello_ack();
   else xnode_handle_command(envelope, envelope["authenticated"] | false);
   DynamicJsonDocument result(8192); result["type"]=event_type; result["payload"]=event.as<JsonVariantConst>();
   std::string output; serializeJson(result,output); std::cout<<output<<std::endl;
  }
  return 0;
 }
 assert(xnode_send_hello_ack());assert(event_type=="helloAck");assert(event.containsKey("meshtasticUser")&&!event.containsKey("meshcoreUser"));assert(event["nodeId"].as<uint32_t>()==0xabcdef01);
 for(const char *type : {"setMeshProtocol","meshSend","selectMeshChannel","setMeshcoreChannel","setMeshcoreUser","setMeshcoreRadio"}) {
   command(type,R"({"protocol":"meshcore","text":"unauthorized"})",false);
   assert(!ok()&&event["error"]=="pairing-required"&&saves==0&&sends==0&&user_saves==0&&channel_saves==0&&!radio_pending);
 }
 command("getMeshProtocol","{}",false);assert(ok());
 command("setMeshProtocol",R"({"protocol":"meshcore","reboot":true})");assert(!ok()&&saves==0&&selected==active);
 command("setMeshProtocol",R"({"protocol":1})");assert(!ok()&&saves==0);
 command("setMeshProtocol",R"({"protocol":"MeshCore"})");assert(!ok()&&saves==0);
 command("setMeshProtocol",R"({"protocol":"meshcore"})");assert(ok()&&saves==1&&selected==MESH_PROTOCOL_MESHCORE&&active==MESH_PROTOCOL_MESHTASTIC&&event["protocolRequiresRestart"]==true&&event["restarted"]==false);
 save_ok=false;command("setMeshProtocol",R"({"protocol":"meshtastic"})");assert(!ok()&&selected==MESH_PROTOCOL_MESHCORE);save_ok=true;
 command("setMeshProtocol",R"({"protocol":"meshtastic","reboot":false})");assert(ok()&&!mesh_protocol_reboot_required());
 command("meshSend",R"({"protocol":"meshcore","text":"wrong protocol"})");assert(!ok()&&sends==0);
 active=selected=MESH_PROTOCOL_MESHCORE;
 assert(xnode_send_hello_ack());assert(!event.containsKey("nodeId")&&!event.containsKey("meshtasticUser"));assert(strlen(event["meshcoreUser"]["publicKey"]|"")==64);assert(event["nativeMeshcoreCompanion"]==false);
 command("setMeshtasticUser",R"({"longName":"Wrong protocol","broadcast":true})");assert(!ok()&&user_saves==0&&broadcasts==0);
 command("setMeshcoreUser",R"({"longName":"MeshCore node","broadcast":true})");assert(ok()&&user_saves==1&&broadcasts==2&&strcmp(user.long_name,"MeshCore node")==0);
 command("setMeshcoreUser",R"({"longName":"bad","isLicensed":true})");assert(!ok()&&user_saves==1);
 command("setMeshcoreUser",R"({"longName":"12345678901234567890123456789012"})");assert(!ok()&&user_saves==1);
 command("setMeshcoreUser",R"({"longName":"line\nbreak"})");assert(!ok()&&user_saves==1);
 command("meshSend",R"({"protocol":"meshcore","text":"hello","dest":123})");assert(!ok()&&sends==0);
 command("meshSend",R"({"protocol":"meshcore","text":"hello","channelSlot":3})");assert(!ok()&&sends==0);
 command("meshSend",R"({"protocol":"meshcore","text":" \n\t"})");assert(!ok()&&sends==0);
 std::string too_long="{\"protocol\":\"meshcore\",\"text\":\""+std::string(201,'x')+"\"}";command("meshSend",too_long.c_str());assert(!ok()&&sends==0);
 command("selectMeshChannel",R"({"protocol":"meshcore","index":256})");assert(!ok()&&channel_index==0);
 command("selectMeshChannel",R"({"protocol":"meshcore","index":-1})");assert(!ok()&&channel_index==0);
 command("selectMeshChannel",R"({"protocol":"meshcore","index":true})");assert(!ok()&&channel_index==0);
 command("selectMeshChannel",R"({"protocol":"meshcore","index":1})");assert(ok()&&channel_index==1);
 command("meshSend",R"({"protocol":"meshcore","text":"hello"})");assert(ok()&&sends==1&&event["queued"]==true&&event["delivered"]==false&&event["channelSlot"]==3);
 ready=false;command("meshSend",R"({"protocol":"meshcore","text":"radio unavailable"})");assert(!ok()&&sends==1);ready=true;
 command("setMeshcoreChannel",R"({"protocol":"meshcore","slot":3,"enabled":true,"name":"Team","keyHex":"8b3387e9c5cdea6ac9e5edbaa115cd72"})");assert(ok()&&channel_saves==1&&channel.psk_len==16&&channel.psk[0]==0x8b);
 command("setMeshcoreChannel",R"({"protocol":"meshcore","slot":3,"enabled":true,"name":"Team","keyHex":"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"})");assert(ok()&&channel_saves==2&&channel.psk_len==32&&channel.psk[31]==31);
 command("setMeshcoreChannel",R"({"protocol":"meshcore","slot":3,"enabled":true,"name":"Team","keyHex":"zb3387e9c5cdea6ac9e5edbaa115cd72"})");assert(!ok()&&channel_saves==2);
 command("setMeshcoreChannel",R"({"protocol":"meshcore","slot":256,"enabled":false})");assert(!ok()&&channel_saves==2);
 command("getMeshChannels",R"({"protocol":"meshcore"})");assert(ok());assert(!event["channels"][0].containsKey("keyHex"));
 command("setMeshcoreRadio",R"({"frequencyMhz":910.525,"bandwidthKhz":62.5,"spreadingFactor":263,"codingRate":5,"txPowerDbm":22})");assert(!ok()&&!radio_pending);
 command("setMeshcoreRadio",R"({"frequencyMhz":868,"bandwidthKhz":62.5,"spreadingFactor":7,"codingRate":5,"txPowerDbm":22})");assert(!ok()&&!radio_pending);
 command("setMeshcoreRadio",R"({"frequencyMhz":915,"bandwidthKhz":125,"spreadingFactor":8,"codingRate":5,"txPowerDbm":20})");assert(ok()&&radio_pending&&event["restarted"]==false&&event["active"]["frequencyMhz"]==radio.frequency_mhz&&event["selected"]["frequencyMhz"]==915);
 xnode_send_meshtastic_rx("FIELD","received");assert(event_type=="meshRx"&&event["protocol"]=="meshcore");
 active=MESH_PROTOCOL_MESHTASTIC;xnode_send_meshtastic_rx("FIELD","received");assert(event_type=="meshtasticRx");
 command("setMeshcoreUser",R"({"longName":"Wrong protocol"})");assert(!ok()&&user_saves==1);
 active=selected=MESH_PROTOCOL_MESHCORE;
 command("setMeshcoreUser",R"({"longName":"Saved name","broadcast":false})");assert(ok()&&event["broadcastQueued"]==true&&user_saves==2&&broadcasts==3);
 // Exact production reassembly binds every fragment to one connection/auth state.
 active=selected=MESH_PROTOCOL_MESHCORE;const unsigned before_frames=sends;
 xnode_rx_frame_t first={},second={};
 strcpy(first.text,"secure:1:2:{\"type\":\"meshSend\",\"payload\":{\"protocol\":\"meshcore\",");
 strcpy(second.text,"secure:2:2:\"text\":\"frame-test\"}}");
 first.connection_epoch=second.connection_epoch=1;first.connection_handle=second.connection_handle=7;first.authenticated=second.authenticated=true;
 xnode_handle_frame(first);xnode_handle_frame(second);assert(sends==before_frames+1);
 first.authenticated=false;xnode_handle_frame(first);xnode_handle_frame(second);assert(sends==before_frames+1);
 first.authenticated=true;second.connection_handle=8;xnode_handle_frame(first);xnode_handle_frame(second);assert(sends==before_frames+1);
 second.connection_handle=7;xnode_handle_frame(first);xnode_on_disconnect();second.connection_epoch=2;xnode_handle_frame(second);assert(sends==before_frames+1);
 xnode_handle_frame(first);assert(sends==before_frames+1); // old queued frame after disconnect
 first.connection_epoch=2;first.authenticated=second.authenticated=false;xnode_handle_frame(first);xnode_handle_frame(second);assert(sends==before_frames+1&&event["error"]=="pairing-required");
 first.authenticated=second.authenticated=true;xnode_handle_frame(first);xnode_handle_frame(second);assert(sends==before_frames+2);
 // Actual NimBLE callback captures authenticated encryption, never encryption alone.
 XnodeCallbacks callback;NimBLECharacteristicCallbacks *callback_api=&callback;NimBLECharacteristic characteristic;
 ble_gap_conn_desc desc={9,{false,false}};
 for(unsigned state=0;state<3;++state){
   desc.sec_state.encrypted=state>=1;desc.sec_state.authenticated=state==2;
   characteristic.value=first.text;callback_api->onWrite(&characteristic,&desc);
   assert(characteristic.value.empty());assert(queued_frames.back().authenticated==(state==2));assert(queued_frames.back().connection_handle==9);
 }
 // Malformed numeric fields and truncated/colliding frame IDs fail closed.
 const unsigned before_bad=sends;
 for (const char *bad : {"bad:1:999999999999999999999:x", "bad:1x:2:x", "bad:-1:2:x", "bad:0:1:x", "bad:1:8193:x", "bad:00001:2:x", "123456789012345678901234:1:1:x"}) {
   xnode_rx_frame_t malformed={};strcpy(malformed.text,bad);malformed.connection_epoch=2;malformed.connection_handle=7;malformed.authenticated=true;
   xnode_handle_frame(first);xnode_handle_frame(malformed);assert(xnode_rx_index==0&&xnode_rx_encoded.empty()&&sends==before_bad);
 }
 uint16_t parsed=0;const char maximum[]="8192";assert(xnode_parse_frame_number(maximum,maximum+4,parsed)&&parsed==8192);
 const size_t queued=queued_frames.size();
 characteristic.value=std::string(256,'x');callback_api->onWrite(&characteristic,&desc);assert(queued_frames.size()==queued&&characteristic.value.empty());
 characteristic.value=std::string("a\0b",3);callback_api->onWrite(&characteristic,&desc);assert(queued_frames.size()==queued&&characteristic.value.empty());
 // Exact production helpers keep peer positions separate from own SOS state.
 xnode_last_lat=43.1;xnode_last_lon=-80.2;xnode_has_location=true;
 for(auto protocol : {MESH_PROTOCOL_MESHTASTIC,MESH_PROTOCOL_MESHCORE}) {
   active=protocol;
   assert(xnode_send_peer_location(51.5,-0.12,"Peer"));
   assert(event_type=="meshPeerLocation"&&event["source"]=="mesh-peer"&&event["protocol"]==xnode_protocol_id(protocol));
   assert(event["lat"]==51.5&&event["lon"]==-0.12&&event["label"]=="Peer");
   assert(xnode_last_lat==43.1&&xnode_last_lon==-80.2&&xnode_has_location&&location_persistence_calls==0);
 }
 active=MESH_PROTOCOL_MESHCORE;
 const char *peer_key="abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
 assert(xnode_send_peer_location(51.5,-0.12,"Identified peer",peer_key));
 assert(strcmp(event["publicKey"]|"",peer_key)==0&&event["idType"]=="ed25519-public-key"&&location_persistence_calls==0);
 assert(!xnode_send_peer_location(0,0,"Bad identity","abcd"));
 assert(!xnode_send_peer_location(0,0,"Bad identity","zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));
 active=MESH_PROTOCOL_MESHTASTIC;assert(!xnode_send_peer_location(0,0,"Wrong protocol",peer_key));
 xnode_has_location=false;assert(xnode_send_peer_location(0,0,nullptr));assert(!xnode_has_location&&location_persistence_calls==0);
 const unsigned before_invalid_location=event_count;
 assert(!xnode_send_peer_location(NAN,0,"Bad")&&!xnode_send_peer_location(0,INFINITY,"Bad")&&!xnode_send_peer_location(91,0,"Bad")&&!xnode_send_peer_location(0,-181,"Bad"));
 assert(event_count==before_invalid_location&&location_persistence_calls==0&&!xnode_has_location);
 assert(xnode_send_location_update(44.25,-79.5,"GPS"));
 assert(event_type=="location"&&xnode_last_lat==44.25&&xnode_last_lon==-79.5&&xnode_has_location&&location_persistence_calls==1);
 const unsigned after_local_location=event_count;
 assert(!xnode_send_location_update(NAN,0,"GPS")&&!xnode_send_location_update(0,181,"GPS"));
 assert(event_count==after_local_location&&xnode_last_lat==44.25&&xnode_last_lon==-79.5&&location_persistence_calls==1);
 assert(xnode_send_peer_location(-33.8,151.2,"Another peer"));
 assert(xnode_last_lat==44.25&&xnode_last_lon==-79.5&&location_persistence_calls==1);
 puts("PASS actual location helpers: peer updates preserve own SOS/check-in location and persistence, local GPS updates remain effective, invalid coordinates rejected");
 puts("PASS production XNODE discovery, staging/no auto-reboot, protocol isolation, full identity, bounded user/channel/radio/text commands, honest queued semantics");
}
