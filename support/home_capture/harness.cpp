// Real LVGL / real firmware home, isolated from physical hardware and services.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include "config.h"
#include "gui/mainbar/main_tile/main_tile.h"
using EventBits_t = unsigned;
using Callback = bool (*)(EventBits_t, void*);
#define log_e(...) ((void)0)
#define log_d(...) ((void)0)
#define log_w(...) ((void)0)
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
#define POWERMGM_WAKEUP 1
#define TIME_SYNC_UPDATE 1
#define SENSOR_TEMPERATURE 1
#define SENSOR_RELHUMIDITY 2
#define _BV(n) (1u << (n))
#define BUTTON_RIGHT 1
static lv_obj_t *root;
static unsigned last_destination;
static bool sensor_available=false;
static bool use_24h=true;
static int battery_percent=87;
static unsigned widget_clicks=0;
static void widget_clicked(lv_obj_t*,lv_event_t event) {if(event==LV_EVENT_CLICKED){++widget_clicks;last_destination=7;}}
using CALLBACK_FUNC=Callback;
struct callback_t {std::vector<Callback> entries;};
callback_t *callback_init(const char*) {return new callback_t;}
bool callback_register(callback_t *table,EventBits_t,Callback cb,const char*) {table->entries.push_back(cb);return true;}
bool callback_send(callback_t *table,EventBits_t event,void *arg) {if(table)for(auto cb:table->entries)cb(event,arg);return true;}
#include "gui/widget_styles.cpp"
lv_obj_t *mainbar_obj_create(lv_obj_t *p) {return lv_obj_create(p,nullptr);}
uint32_t mainbar_add_tile(int,int,const char*,lv_style_t*) {return 1;}
lv_obj_t *mainbar_get_tile_obj(uint32_t) {return root;}
void mainbar_add_slide_element(lv_obj_t *obj) {
    // Same LVGL operation used by real mainbar_add_slide_element via
    // lv_tileview_add_element: critical because it makes labels draggable.
    #if !defined(LILYGO_T_DECK_PRO)
    lv_page_glue_obj(obj,true);
    #endif
}
void mainbar_jump_to_tilenumber(uint32_t n,lv_anim_enable_t) {last_destination=n;}
void mainbar_jump_to_tilenumber(uint32_t n,lv_anim_enable_t,bool) {last_destination=n;}
void mainbar_clear_history() {}
void mainbar_add_tile_button_cb(uint32_t,Callback) {}
uint32_t app_tile_get_tile_num() {return 2;}
uint32_t note_tile_get_tile_num() {return 3;}
uint32_t setup_tile_get_tile_num() {return 4;}
uint32_t osmmap_app_get_tile_num() {return 5;}
uint32_t setup_get_tile_num() {return 4;}
uint32_t osmmap_app_get_app_main_tile_num() {return 5;}
void meshtastic_app_open() {last_destination=6;}
bool sensor_get_available() {return sensor_available;}
bool timesync_get_24hr() {return use_24h;}
time_t timesync_get_build_epoch_utc() {return 1790618400;}
int pmu_get_battery_percent() {return battery_percent;}
bool pmu_is_charging() {return false;}
bool pmu_is_vbus_plug() {return false;}
void timesync_get_current_timestring(char *b,size_t n) {snprintf(b,n,use_24h?"14:32":"2:32");}
#define REGISTER(name) void name(EventBits_t,Callback,const char*) {}
REGISTER(pmu_register_cb) REGISTER(blectl_register_cb) REGISTER(wifictl_register_cb)
REGISTER(powermgm_register_cb) REGISTER(timesync_register_cb) REGISTER(sensor_register_cb)
void wf_label_printf(lv_obj_t *o,const char *fmt,...) {char b[128]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof b,fmt,a); va_end(a); lv_label_set_text(o,b);}
// MinGW compatibility. Deterministic clock is synthetic demonstration data.
static tm *capture_localtime(const time_t *t,tm *out) {*out=*gmtime(t);return out;}
static time_t capture_epoch=1790605920;
static time_t capture_time(time_t *out) {if(out)*out=capture_epoch;return capture_epoch;}
#define localtime_r capture_localtime
#define time capture_time
#include "gui/mainbar/main_tile/main_tile.cpp"
#undef time
static int width,height;
static std::vector<unsigned char> pixels;
static lv_point_t pointer_point;
static lv_indev_state_t pointer_state=LV_INDEV_STATE_REL;
static bool pointer_read(lv_indev_drv_t*,lv_indev_data_t *data) {
    data->point=pointer_point;data->state=pointer_state;return false;
}
static void pointer_click(int x,int y) {
    pointer_point.x=x;pointer_point.y=y;
    pointer_state=LV_INDEV_STATE_PR;
    for(int i=0;i<4;i++){lv_tick_inc(20);lv_task_handler();}
    pointer_state=LV_INDEV_STATE_REL;
    for(int i=0;i<4;i++){lv_tick_inc(20);lv_task_handler();}
}
static void check_message_pointer() {
    lv_area_t b,label,envelope;lv_obj_get_coords(home_widget_buttons[0],&b);
    lv_obj_get_coords(home_widget_labels[0],&label);lv_obj_get_coords(home_widget_envelopes[0],&envelope);
    std::vector<lv_point_t> points={{(lv_coord_t)((label.x1+label.x2)/2),(lv_coord_t)((label.y1+label.y2)/2)},
        {(lv_coord_t)((envelope.x1+envelope.x2)/2),(lv_coord_t)((envelope.y1+envelope.y2)/2)},
        {(lv_coord_t)(b.x1+2),(lv_coord_t)((b.y1+b.y2)/2)},
        {(lv_coord_t)(b.x2-2),(lv_coord_t)((b.y1+b.y2)/2)},
        {(lv_coord_t)((b.x1+b.x2)/2),(lv_coord_t)(b.y1+2)},
        {(lv_coord_t)((b.x1+b.x2)/2),(lv_coord_t)(b.y2-2)}};
    if(!lv_obj_get_hidden(home_widget_badges[0])) {
        lv_area_t badge;lv_obj_get_coords(home_widget_badges[0],&badge);
        points.push_back({(lv_coord_t)((badge.x1+badge.x2)/2),(lv_coord_t)((badge.y1+badge.y2)/2)});
    }
    for(auto p:points) {
        unsigned before=widget_clicks;last_destination=0;pointer_click(p.x,p.y);
        if(widget_clicks!=before+1 || last_destination!=7)fprintf(stderr,"Messages pointer failed %dx%d at %d,%d: callback delta %d destination %u\n",width,height,p.x,p.y,int(widget_clicks-before),last_destination);
        assert(widget_clicks==before+1 && last_destination==7);
    }
}
static void check_nav_pointer() {
    const unsigned destinations[]={5,6,2,4};
    for(int i=0;i<4;i++) {
        if(lv_obj_get_hidden(home_nav[i]))continue;
        lv_area_t b;lv_obj_get_coords(home_nav[i],&b);
        for(int x:{(b.x1+b.x2)/2,b.x1+2,b.x2-2}) {
            last_destination=0;pointer_click(x,(b.y1+b.y2)/2);
            assert(last_destination==destinations[i]);
        }
    }
}
static bool visible(lv_obj_t *obj) {
    for(;obj;obj=lv_obj_get_parent(obj))if(lv_obj_get_hidden(obj))return false;
    return true;
}
static void check_shared_theme(bool light) {
    #if !defined(LILYGO_T_DECK_PRO)
    const lv_color_t expected=light?LV_COLOR_WHITE:WS_TACTICAL_DARK_COLOR;
    lv_style_t *surfaces[]={ws_get_background_style(),ws_get_mainbar_style(),ws_get_app_style(),ws_get_app_opa_style(),ws_get_setup_tile_style()};
    for(auto surface:surfaces) {
        lv_obj_t *panel=lv_obj_create(lv_scr_act(),nullptr);
        lv_obj_reset_style_list(panel,LV_OBJ_PART_MAIN);lv_obj_add_style(panel,LV_OBJ_PART_MAIN,surface);
        assert(lv_obj_get_style_bg_color(panel,LV_OBJ_PART_MAIN).full==expected.full);
        lv_obj_del(panel);
    }
    assert(lv_obj_get_style_bg_color(root,LV_OBJ_PART_MAIN).full==expected.full);
    #endif
    assert(!visible(batteryicon) && !visible(batterylabel));
    assert(!visible(home_clock_mode));
}
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *c) {
    for(int y=a->y1;y<=a->y2;y++) for(int x=a->x1;x<=a->x2;x++) {
        lv_color32_t rgb; rgb.full=lv_color_to32(*c++);
        if(x>=0 && x<width && y>=0 && y<height) {size_t i=(y*width+x)*3;pixels[i]=rgb.ch.red;pixels[i+1]=rgb.ch.green;pixels[i+2]=rgb.ch.blue;}
    }
    lv_disp_flush_ready(d);
}
static void check_content_bounds() {
    lv_area_t date,rule,clock;
    lv_obj_get_coords(datelabel,&date);lv_obj_get_coords(home_rules[2],&rule);lv_obj_get_coords(timelabel,&clock);
    assert(clock.x1>=0 && clock.x2<width);
    assert(date.y2<rule.y1);
    assert(date.x1>=0 && date.x2<width);
    assert(clock.y2<date.y1);
    for(int i=0;i<4+MAX_WIDGET_NUM;i++) {
        lv_obj_t *button=i<4?home_nav[i]:home_widget_buttons[i-4];
        if(lv_obj_get_hidden(button))continue;
        lv_obj_t *label=i<4?lv_obj_get_child(button,nullptr):home_widget_labels[i-4];
        lv_area_t b,l;lv_obj_get_coords(button,&b);lv_obj_get_coords(label,&l);
        if(!(l.x1>=b.x1 && l.x2<=b.x2 && l.y1>=b.y1 && l.y2<=b.y2)) fprintf(stderr,"Control %d '%s' outside parent at %dx%d: label %d,%d-%d,%d parent %d,%d-%d,%d\n",i,lv_label_get_text(label),width,height,l.x1,l.y1,l.x2,l.y2,b.x1,b.y1,b.x2,b.y2);
        assert(l.x1>=b.x1 && l.x2<=b.x2 && l.y1>=b.y1 && l.y2<=b.y2);
        assert(b.x1>=0 && b.x2<width && b.y1>=0 && b.y2<height);
    }
    #if defined(MAIN_TILE_HAS_MOON)
    lv_area_t moon;lv_obj_get_coords(moonlabel,&moon);
    if(moon.y2>=rule.y1)fprintf(stderr,"Moon overlaps separator at %dx%d: moon bottom %d separator %d\n",width,height,moon.y2,rule.y1);
    assert(moon.y2<rule.y1);
    assert(moon.x1>=0 && moon.x2<width);
    #endif
    if(sensor_available) {
        lv_area_t sensor;lv_obj_get_coords(templabel,&sensor);
        if(date.y2>=sensor.y1)fprintf(stderr,"Date/sensor overlap at %dx%d: date bottom %d sensor top %d\n",width,height,date.y2,sensor.y1);
        assert(sensor.y2<rule.y1 && sensor.x1>=0 && sensor.x2<width);
        assert(date.y2<sensor.y1);
        #if defined(MAIN_TILE_HAS_MOON)
        if(moon.y2>=sensor.y1)fprintf(stderr,"Moon/sensor overlap at %dx%d: moon bottom %d sensor top %d\n",width,height,moon.y2,sensor.y1);
        assert(moon.y2<sensor.y1);
        #endif
    }
}
int main(int argc,char **argv) {
    assert(argc==5); width=atoi(argv[1]);height=atoi(argv[2]); bool light=strcmp(argv[3],"light")==0;
    pixels.resize(width*height*3);lv_init();
    static lv_disp_buf_t buf;std::vector<lv_color_t> buffer(width*height);lv_disp_buf_init(&buf,buffer.data(),nullptr,buffer.size());
    lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.hor_res=width;driver.ver_res=height;driver.buffer=&buf;driver.flush_cb=flush;lv_disp_drv_register(&driver);
    lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=pointer_read;lv_indev_drv_register(&input);
    lv_style_t *shared_style=ws_get_mainbar_style();widget_style_theme_set(light?0:1);
    root=lv_obj_create(lv_scr_act(),nullptr);lv_obj_reset_style_list(root,LV_OBJ_PART_MAIN);lv_obj_add_style(root,LV_OBJ_PART_MAIN,shared_style);lv_obj_set_size(root,width,height);lv_obj_set_pos(root,0,0);
    main_tile_setup();main_tile_update_time(true);
    check_shared_theme(light);
    // Verify a legacy copied surface follows both directions after binding.
    static lv_style_t legacy_surface;lv_style_init(&legacy_surface);lv_style_copy(&legacy_surface,ws_get_mainbar_style());ws_bind_theme_surface(&legacy_surface);
    for(int selected:{0,1,0,1}) {
        widget_style_theme_set(selected);check_shared_theme(selected==0);
        lv_color_t actual,expected;_lv_style_get_color(&legacy_surface,LV_STYLE_BG_COLOR,&actual);_lv_style_get_color(ws_get_mainbar_style(),LV_STYLE_BG_COLOR,&expected);
        assert(actual.full==expected.full);
    }
    widget_style_theme_set(light?0:1);
    int32_t pmu=87;mainbar_pmu_event_cb(PMUCTL_STATUS,&pmu);
    mainbar_wifictl_event_cb(WIFICTL_ON,nullptr);mainbar_wifictl_event_cb(WIFICTL_CONNECT,nullptr);
    mainbar_blectl_event_cb(BLECTL_ON,nullptr);mainbar_blectl_event_cb(BLECTL_CONNECT,nullptr);
    assert(strcmp(lv_label_get_text(home_wifi),"WI-FI LINK")==0);
    assert(strcmp(lv_label_get_text(home_ble),"BLE LINK")==0);
    mainbar_wifictl_event_cb(WIFICTL_DISCONNECT,nullptr);mainbar_blectl_event_cb(BLECTL_DISCONNECT,nullptr);
    assert(strcmp(lv_label_get_text(home_wifi),"WI-FI IDLE")==0);
    assert(strcmp(lv_label_get_text(home_ble),"BLE IDLE")==0);
    mainbar_wifictl_event_cb(WIFICTL_OFF,nullptr);mainbar_blectl_event_cb(BLECTL_OFF,nullptr);
    assert(strcmp(lv_label_get_text(home_wifi),"WI-FI OFF")==0);
    assert(strcmp(lv_label_get_text(home_ble),"BLE OFF")==0);
    const unsigned destinations[]={5,6,2,4};
    for(int i=0;i<4;i++) {lv_event_send(home_nav[i],LV_EVENT_CLICKED,nullptr);assert(last_destination==destinations[i]);}
    pmu=87|PMUCTL_STATUS_CHARGING;mainbar_pmu_event_cb(PMUCTL_STATUS,&pmu);
    pmu=87;mainbar_pmu_event_cb(PMUCTL_STATUS,&pmu);
    sensor_available=true;float temp=18.4f,humidity=63;main_tile_sensor_event_cb(SENSOR_TEMPERATURE,&temp);main_tile_sensor_event_cb(SENSOR_RELHUMIDITY,&humidity);
    assert(!lv_obj_get_hidden(templabel));
    sensor_available=false;main_tile_align_widgets();assert(lv_obj_get_hidden(templabel));
    use_24h=false;main_tile_update_time(true);assert(strcmp(lv_label_get_text(timelabel),"2:32")==0);check_content_bounds();
    use_24h=true;main_tile_update_time(true);assert(strcmp(lv_label_get_text(timelabel),"14:32")==0);
    capture_epoch=0;main_tile_update_time(true);assert(visible(home_clock_mode));assert(strcmp(lv_label_get_text(home_clock_mode),"SET CLOCK")==0);check_content_bounds();
    capture_epoch=1790605920;main_tile_update_time(true);
    pmu=100;mainbar_pmu_event_cb(PMUCTL_STATUS,&pmu);check_content_bounds();
    pmu=87;mainbar_pmu_event_cb(PMUCTL_STATUS,&pmu);
    lv_label_set_text(widget_entry[0].label,"message");lv_label_set_text(widget_entry[0].ext_label,"");lv_obj_set_event_cb(widget_entry[0].icon_img,widget_clicked);
    main_tile_register_widget();
    // First check untouched production objects so the probe below cannot
    // accidentally repair a regression before it is tested.
    check_message_pointer();
    #if !defined(LILYGO_T_DECK_PRO)
    // Fault-inject the exact original registration behavior. A glued label
    // becomes clickable and swallows the center tap; prove this regression
    // would be caught, then restore the production label configuration.
    lv_area_t message_text;lv_obj_get_coords(home_widget_labels[0],&message_text);
    unsigned before_fault=widget_clicks;
    lv_page_glue_obj(home_widget_labels[0],true);
    pointer_click((message_text.x1+message_text.x2)/2,(message_text.y1+message_text.y2)/2);
    assert(widget_clicks==before_fault);
    lv_page_glue_obj(home_widget_labels[0],false);lv_obj_set_click(home_widget_labels[0],false);
    #endif
    check_message_pointer();
    // Exercise the real badge child with a non-sensitive synthetic indicator.
    lv_img_set_src(widget_entry[0].icon_indicator,LV_SYMBOL_WARNING);lv_obj_set_hidden(widget_entry[0].icon_indicator,false);
    main_tile_align_widgets();assert(!lv_obj_get_hidden(home_widget_badges[0]));check_message_pointer();
    lv_obj_set_hidden(widget_entry[0].icon_indicator,true);main_tile_align_widgets();
    check_nav_pointer();
    main_tile_button_event_cb(BUTTON_RIGHT,nullptr);assert(last_destination==2);
    main_tile_style_event_cb(STYLE_CHANGE,nullptr);
    check_content_bounds();
    // These four actual targets have no temperature/humidity provider;
    // sensor_get_available() is true only for legacy M5PAPER.
    for(int i=1;i<3;i++) {
        lv_label_set_text(widget_entry[i].label,i==1?"GPS":"WX");
        lv_label_set_text(widget_entry[i].ext_label,"");main_tile_register_widget();
    }
    check_content_bounds();
    for(int i=1;i<3;i++)widget_entry[i].active=false;
    main_tile_align_widgets();
    lv_obj_invalidate(root);lv_refr_now(nullptr);
    FILE *f=fopen(argv[4],"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",width,height);fwrite(pixels.data(),1,pixels.size(),f);fclose(f);
}
