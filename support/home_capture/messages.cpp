// Real home, widget removal, message-entry/open-latest function bodies and
// mainbar navigation. Message content rendering is a spy, not a replacement
// destination: assertions inspect the actual LVGL/mainbar active tile.
#define XNODE_MESSAGES_HARNESS
#include "navigation.cpp"
#include <cstdarg>
#include <ctime>
#define PMUCTL_STATUS 1
#define PMUCTL_STATUS_PERCENT 0xff
#define PMUCTL_STATUS_CHARGING 0x200
#define BLECTL_ON 16
#define BLECTL_OFF 32
#define BLECTL_CONNECT 1
#define BLECTL_DISCONNECT 2
#define WIFICTL_ON 8
#define WIFICTL_OFF 16
#define WIFICTL_CONNECT 1
#define WIFICTL_DISCONNECT 4
#define TIME_SYNC_UPDATE 1
#define SENSOR_TEMPERATURE 1
#define SENSOR_RELHUMIDITY 2
bool sensor_get_available(){return false;}
bool timesync_get_24hr(){return true;}
time_t timesync_get_build_epoch_utc(){return 1790605920;}
int pmu_get_battery_percent(){return 87;}
bool pmu_is_charging(){return false;}
bool pmu_is_vbus_plug(){return false;}
void timesync_get_current_timestring(char*b,size_t n){snprintf(b,n,"14:32");}
uint32_t osmmap_app_get_app_main_tile_num(){return 1;}
void meshtastic_app_open(){}
#define REGISTER(name) void name(EventBits_t,CALLBACK_FUNC,const char*){}
REGISTER(pmu_register_cb) REGISTER(blectl_register_cb) REGISTER(wifictl_register_cb) REGISTER(powermgm_register_cb) REGISTER(timesync_register_cb) REGISTER(sensor_register_cb)
void wf_label_printf(lv_obj_t*o,const char*fmt,...){char b[128];va_list a;va_start(a,fmt);vsnprintf(b,sizeof b,fmt,a);va_end(a);lv_label_set_text(o,b);}
static tm *capture_localtime(const time_t*t,tm*out){*out=*gmtime(t);return out;}
#define localtime_r capture_localtime
#include "gui/mainbar/main_tile/main_tile.cpp"
#include "gui/widget.cpp"
struct MessageFixture{int count;};
static MessageFixture fixture={2};
static MessageFixture *bluetooth_msg_chain=&fixture;
int msg_chain_get_entrys(MessageFixture*c){return c->count;}
static icon_t *messages_widget=nullptr;
static bool bluetooth_message_new=false;
static bool bluetooth_message_tile_active=false;
static uint32_t bluetooth_message_tile_num;
static lv_obj_t *bluetooth_message_exit_btn,*bluetooth_message_prev_msg_btn,*bluetooth_message_next_msg_btn,*bluetooth_message_trash_msg_btn;
void wf_image_button_fade_in(lv_obj_t*,uint32_t,uint32_t){}
static void bluetooth_message_configure_nav_buttons(){}
static int rendered_entry=-1;
static void bluetooth_message_mark_read();
static void bluetooth_message_show_msg(int entry){rendered_entry=entry;if(bluetooth_message_tile_active)bluetooth_message_mark_read();}
static bool legacy_guard=false;
static void message_jump(uint32_t id,lv_anim_enable_t anim,bool hidden){
    // Exact rejected-current-coordinate condition from the old mainbar. This
    // fault injection is confined to the native test; production is untouched.
    if(legacy_guard){auto p=tile_pos_table[active()];for(unsigned i=0;i<mainbar_history->entrys;i++)if(mainbar_history->tile[i].x==p.x&&mainbar_history->tile[i].y==p.y)return;}
    mainbar_jump_to_tilenumber(id,anim,hidden);
}
#define mainbar_jump_to_tilenumber message_jump
#include "message_workflow.inc"
#undef mainbar_jump_to_tilenumber
static void notify_fixture(){bluetooth_message_new=true;messages_widget=widget_register("message",&message_64px,enter_bluetooth_messages_cb);assert(messages_widget);widget_set_indicator(messages_widget,ICON_INDICATOR_2);}
static void seed_revisited_home_history(){mainbar_jump_to_maintile(LV_ANIM_OFF);mainbar_history->entrys=1;mainbar_history->tile[0]={0,0};mainbar_history->tile[1]=tile_pos_table[app_tile_get_tile_num()];mainbar_history->anim[1]=LV_ANIM_OFF;mainbar_history->statusbar[1]=false;mainbar_history->powermgm_state[1]=POWERMGM_WAKEUP;}
int main(int argc,char**argv){
    assert(argc==5);width=atoi(argv[1]);height=atoi(argv[2]);lv_init();lv_png_init();pixels.resize(width*height*3);
    std::vector<lv_color_t> buffer(width*height);static lv_disp_buf_t buf;lv_disp_buf_init(&buf,buffer.data(),nullptr,buffer.size());lv_disp_drv_t dd;lv_disp_drv_init(&dd);dd.hor_res=width;dd.ver_res=height;dd.buffer=&buf;dd.flush_cb=flush;lv_disp_drv_register(&dd);
    lv_indev_drv_t id;lv_indev_drv_init(&id);id.type=LV_INDEV_TYPE_POINTER;id.read_cb=read_pointer;lv_indev_drv_register(&id);
    ws_get_mainbar_style();widget_style_theme_set(strcmp(argv[3],"light")==0?0:1);lv_obj_add_style(lv_scr_act(),LV_OBJ_PART_MAIN,ws_get_background_style());
    mainbar_setup();main_tile_setup();main_tile_update_time(true);app_tile_setup();setup_tile_setup();notes_number=mainbar_add_tile(1+MAX_APPS_TILES+MAX_SETUP_TILES,0,"notes",ws_get_mainbar_style());
    bluetooth_message_tile_num=mainbar_add_app_tile(1,1,"bluetooth message");
    auto msg_tile=mainbar_get_tile_obj(bluetooth_message_tile_num);
    bluetooth_message_exit_btn=lv_btn_create(msg_tile,nullptr);bluetooth_message_prev_msg_btn=lv_btn_create(msg_tile,nullptr);bluetooth_message_next_msg_btn=lv_btn_create(msg_tile,nullptr);bluetooth_message_trash_msg_btn=lv_btn_create(msg_tile,nullptr);
    mainbar_add_tile_activate_cb(bluetooth_message_tile_num,bluetooth_message_activate_cb);mainbar_add_tile_hibernate_cb(bluetooth_message_tile_num,bluetooth_message_hibernate_cb);
    app_register("messages",&message_64px,enter_bluetooth_messages_cb);mainbar_finalize_menu_pages();
    // Reproduce both original ingredients: mark read before navigation + old
    // history guard. This is the observed disappearing-box/still-home failure.
    seed_revisited_home_history();notify_fixture();legacy_guard=true;
    bluetooth_message_mark_read();bluetooth_message_open_latest();
    assert(active()==0&&messages_widget==nullptr&&!bluetooth_message_new);
    // Current open-latest must preserve unread state when navigation rejects.
    notify_fixture();click(home_widget_labels[0]);assert(active()==0&&messages_widget!=nullptr&&bluetooth_message_new&&!bluetooth_message_tile_active);
    // Restoring the real current mainbar routes the SAME real pointer callback
    // into the actual messages tile, then the activation clears the widget.
    legacy_guard=false;click(home_widget_labels[0]);assert(active()==bluetooth_message_tile_num&&bluetooth_message_tile_active);assert(rendered_entry==1);assert(messages_widget==nullptr&&!bluetooth_message_new);
    mainbar_jump_back();assert(active()==0&&!bluetooth_message_tile_active);
    for(int repeat=0;repeat<3;repeat++){notify_fixture();click(home_widget_buttons[0]);assert(active()==bluetooth_message_tile_num&&rendered_entry==1&&bluetooth_message_tile_active);assert(messages_widget==nullptr);assert(bluetooth_message_open_latest());assert(active()==bluetooth_message_tile_num);mainbar_jump_back();assert(active()==0&&!bluetooth_message_tile_active);}
    puts("PASS actual message-open function bodies + home pointer + real widget removal/mainbar destination, rejected navigation preserves unread, repeated open/back");
}
