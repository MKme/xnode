// Actual mainbar, app/setup tile builders and icon registration. Hardware only
// is stubbed; pointer gestures go through LVGL's input and tileview engines.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "config.h"
#include "gui/png_decoder/lv_png.h"
using EventBits_t=unsigned;
using CALLBACK_FUNC=bool(*)(EventBits_t,void*);
#define _BV(n) (1u<<(n))
#define log_e(...) ((void)0)
#define log_d(...) ((void)0)
#define log_w(...) ((void)0)
#define log_i(...) ((void)0)
#define ASSERT(v,...) assert(v)
#define MALLOC_ASSERT(n,...) calloc(1,n)
#define REALLOC_ASSERT(p,n,...) realloc(p,n)
#define BUTTON_UP 1
#define BUTTON_RIGHT 2
#define BUTTON_LEFT 4
#define BUTTON_DOWN 8
#define BUTTON_ENTER 16
#define BUTTON_EXIT 32
#define BUTTON_REFRESH 64
#define BUTTON_SETUP 128
#define BUTTON_MENU 256
#define BUTTON_KEYBOARD 512
#define POWERMGM_STANDBY 1
#define POWERMGM_WAKEUP 2
#define POWERMGM_SILENCE_WAKEUP 4
#define POWERMGM_STANDBY_REQUEST 8
#define POWERMGM_WAKEUP_REQUEST 16
#define POWERMGM_SILENCE_WAKEUP_REQUEST 32
#define RTCCTL_ALARM_OCCURRED 1
#define CALL_CB_FIRST 0
#define CALL_CB_LAST 1
#define STATUSBAR_HEIGHT 26
struct callback_t {std::vector<CALLBACK_FUNC> entries;};
callback_t *callback_init(const char*){return new callback_t;}
bool callback_register(callback_t*t,EventBits_t,CALLBACK_FUNC cb,const char*){t->entries.push_back(cb);return true;}
bool callback_send(callback_t*t,EventBits_t e,void*a){if(t)for(auto cb:t->entries)cb(e,a);return true;}
void powermgm_register_cb_with_prio(EventBits_t,CALLBACK_FUNC,const char*,int){}
void rtcctl_register_cb(EventBits_t,CALLBACK_FUNC,const char*){}
void button_register_cb(EventBits_t,CALLBACK_FUNC,const char*){}
void display_note_activity(){}
bool display_get_block_return_maintile(){return false;}
unsigned powermgm_get_event(unsigned){return POWERMGM_WAKEUP;}
void powermgm_set_event(unsigned){}
static bool status_hidden=false;
bool statusbar_get_hidden_state(){return status_hidden;}
void statusbar_hide(bool h){status_hidden=h;}
void statusbar_expand(bool){}
#ifdef XNODE_MESH_HARNESS
void keyboard_hide();
#else
void keyboard_hide(){}
#endif
void gui_force_redraw(bool){}
uint32_t millis(){return lv_tick_get();}
struct SerialMock{template<class...T>void printf(const char*,T...){}}Serial;
struct touch_t{bool touched;int16_t x_coor,y_coor;};
bool touch_get_swipe_delta(int8_t*,int8_t*){return false;}
bool touch_get_last(touch_t&){return false;}
#include "gui/widget_styles.cpp"
#include "../../src/gui/widget_factory.h"
#include "gui/mainbar/mainbar.cpp"
#include "gui/mainbar/app_tile/app_tile.cpp"
#include "gui/mainbar/setup_tile/setup_tile.cpp"
#include "gui/tactical_icons.cpp"
#include "gui/app.cpp"
#include "gui/setup.cpp"
static uint32_t notes_number;
#ifndef XNODE_MESSAGES_HARNESS
uint32_t main_tile_get_tile_num(){return 0;}
#endif
uint32_t note_tile_get_tile_num(){return notes_number;}
LV_IMG_DECLARE(message_64px);
LV_IMG_DECLARE(gps_64px);
LV_IMG_DECLARE(wifi_64px);
LV_IMG_DECLARE(bluetooth_64px);
LV_IMG_DECLARE(osm_64px);
LV_IMG_DECLARE(location_64px);
LV_IMG_DECLARE(notification_64px);
LV_IMG_DECLARE(brightness_64px);
LV_IMG_DECLARE(time_64px);
LV_IMG_DECLARE(battery_icon_64px);
LV_IMG_DECLARE(sound_64px);
LV_IMG_DECLARE(gps_status_64px);LV_IMG_DECLARE(calc_app_64px);LV_IMG_DECLARE(owm01d_64px);LV_IMG_DECLARE(compass_64px);LV_IMG_DECLARE(stopwatch_app_64px);LV_IMG_DECLARE(tracker_64px);
LV_IMG_DECLARE(style_64px);LV_IMG_DECLARE(move_64px);LV_IMG_DECLARE(sdcard_settings_64px);LV_IMG_DECLARE(utilities_64px);LV_IMG_DECLARE(watchface_64px);
static int width,height;
static std::vector<unsigned char> pixels;
static lv_point_t pointer;
static lv_indev_state_t pointer_state=LV_INDEV_STATE_REL;
static bool read_pointer(lv_indev_drv_t*,lv_indev_data_t*d){d->point=pointer;d->state=pointer_state;return false;}
static void pump(int count=4){for(int i=0;i<count;i++){lv_tick_inc(20);lv_task_handler();}}
static void swipe(int direction){
    pointer={(lv_coord_t)(direction<0?width-25:25),(lv_coord_t)(height-35)};pointer_state=LV_INDEV_STATE_PR;pump();
    int start=pointer.x,end=direction<0?25:width-25;
    for(int i=1;i<=12;i++){pointer.x=start+(end-start)*i/12;pump(2);}
    pointer_state=LV_INDEV_STATE_REL;pump(30);
}
static void swipe_vertical(){pointer={(lv_coord_t)(width/2),(lv_coord_t)(height-30)};pointer_state=LV_INDEV_STATE_PR;pump();int start=pointer.y;for(int i=1;i<=12;i++){pointer.y=start+(40-start)*i/12;pump(2);}pointer_state=LV_INDEV_STATE_REL;pump(30);}
static void click_point(int x,int y){pointer={(lv_coord_t)x,(lv_coord_t)y};pointer_state=LV_INDEV_STATE_PR;pump();pointer_state=LV_INDEV_STATE_REL;pump();}
static void click(lv_obj_t *obj){lv_area_t a;lv_obj_get_coords(obj,&a);click_point((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
static unsigned active(){
    #if defined(LILYGO_T_DECK_PRO)
    return mainbar_tdeck_pro_active_tile;
    #else
    lv_coord_t x,y;lv_tileview_get_tile_act(mainbar,&x,&y);for(unsigned i=0;i<tile_entrys;i++)if(tile_pos_table[i].x==x&&tile_pos_table[i].y==y)return i;assert(false);return 0;
    #endif
}
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*c){for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){lv_color32_t rgb;rgb.full=lv_color_to32(*c++);if(x>=0&&x<width&&y>=0&&y<height){size_t n=(y*width+x)*3;pixels[n]=rgb.ch.red;pixels[n+1]=rgb.ch.green;pixels[n+2]=rgb.ch.blue;}}lv_disp_flush_ready(d);}
static void save(const char *prefix,const char *suffix){lv_refr_now(nullptr);char path[2048];snprintf(path,sizeof path,"%s-%s.ppm",prefix,suffix);FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",width,height);fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}
static unsigned destination,clicks,activations,hibernations;
static lv_obj_t *expected_click_obj=nullptr;
static void open_demo(lv_obj_t*obj,lv_event_t e){if(e==LV_EVENT_CLICKED){if(expected_click_obj)assert(obj==expected_click_obj);clicks++;mainbar_jump_to_tilenumber(destination,LV_ANIM_OFF);}}
static void check_icon_targets(icon_t *icon,unsigned source_page){
    for(auto obj:{icon->icon_cont,icon->label,icon->icon_img}){
        mainbar_jump_to_tilenumber(source_page,LV_ANIM_OFF);mainbar_clear_history();pump();unsigned before=clicks;click(obj);
        assert(clicks==before+1&&active()==destination);mainbar_jump_back();assert(active()==source_page);
    }
    lv_area_t card;lv_obj_get_coords(icon->icon_cont,&card);unsigned before=clicks;
    click_point(card.x1+2,card.y1+2);assert(clicks==before+1&&active()==destination);mainbar_jump_back();assert(active()==source_page);
}
static void activated(){activations++;}
static void hibernated(){hibernations++;}
#if !defined(XNODE_MESSAGES_HARNESS) && !defined(XNODE_MESH_HARNESS)
int main(int argc,char**argv){
    assert(argc==5);width=atoi(argv[1]);height=atoi(argv[2]);bool light=strcmp(argv[3],"light")==0;lv_init();lv_png_init();pixels.resize(width*height*3);
    std::vector<lv_color_t> buffer(width*height);static lv_disp_buf_t buf;lv_disp_buf_init(&buf,buffer.data(),nullptr,buffer.size());lv_disp_drv_t dd;lv_disp_drv_init(&dd);dd.hor_res=width;dd.ver_res=height;dd.buffer=&buf;dd.flush_cb=flush;lv_disp_drv_register(&dd);
    lv_indev_drv_t id;lv_indev_drv_init(&id);id.type=LV_INDEV_TYPE_POINTER;id.read_cb=read_pointer;lv_indev_drv_register(&id);
    ws_get_mainbar_style();widget_style_theme_set(light?0:1);lv_obj_add_style(lv_scr_act(),LV_OBJ_PART_MAIN,ws_get_background_style());mainbar_setup();mainbar_add_tile(0,0,"home",ws_get_mainbar_style());app_tile_setup();setup_tile_setup();
    notes_number=mainbar_add_tile(1+MAX_APPS_TILES+MAX_SETUP_TILES,0,"notes",ws_get_mainbar_style());
    for(unsigned i=0;i<tile_entrys;i++){assert(tile_pos_table[i].y==0);assert(tile_pos_table[i].x==i);}
    destination=mainbar_add_app_tile(2,2,"test multirow app");
    unsigned setup_group=mainbar_add_setup_tile(1,3,"test setup submenu");
    for(unsigned i=1;i<4;i++){assert(tile_pos_table[destination+i].y==tile_pos_table[destination].y);assert(tile_pos_table[destination+i].x==tile_pos_table[destination].x+i);}
    for(unsigned i=1;i<3;i++){assert(tile_pos_table[setup_group+i].y==tile_pos_table[setup_group].y);assert(tile_pos_table[setup_group+i].x==tile_pos_table[setup_group].x+i);}
    // Exact existing registration labels/art from bluetooth_message, meshtastic,
    // osmmap, xnode_checkin, xnode_notifications and xnode_sos implementations.
    const char *names[]={"messages","mesh","Tac\nMap","CheckIn","Alert\nSummary","SOS","gps status","Calculator","weather","compass","stop\nwatch","gps tracker"};
    const lv_img_dsc_t*icons[]={&message_64px,&message_64px,&osm_64px,&location_64px,&notification_64px,&notification_64px,&gps_status_64px,&calc_app_64px,&owm01d_64px,&compass_64px,&stopwatch_app_64px,&tracker_64px};
    for(int i=0;i<12;i++)assert(app_register(names[i],icons[i],open_demo));
    const char *settings[]={"display","wifi","bluetooth","time","battery","sound","themes","move","SD card","Utilities","watchface","notify\nsettings"};
    const lv_img_dsc_t *settings_icons[]={&brightness_64px,&wifi_64px,&bluetooth_64px,&time_64px,&battery_icon_64px,&sound_64px,&style_64px,&move_64px,&sdcard_settings_64px,&utilities_64px,&watchface_64px,&notification_64px};
    for(int i=0;i<12;i++)assert(setup_register(settings[i],settings_icons[i],open_demo));
    for(int i=0;i<12;i++) {
        lv_area_t label;lv_obj_get_coords(app_entry[i].label,&label);
        if(label.y2>=height-16)fprintf(stderr,"Apps caption '%s' overlaps footer at %dx%d: bottom %d, footer zone %d\n",names[i],width,height,label.y2,height-16);
        assert(label.y2<height-16);
        lv_obj_get_coords(setup_entry[i].label,&label);assert(label.y2<height-16);
    }
    icon_t *late=app_register("Late callback",&message_64px,nullptr);assert(late);lv_obj_set_event_cb(late->icon_img,open_demo);
    unsigned late_index=unsigned(late-app_entry),late_page=app_tile_num[late_index/(MAX_APPS_ICON_HORZ*MAX_APPS_ICON_VERT)];
    // Behavior-only registration must not affect authentic sample captures or
    // their occupied-page counters. Restore it for the subsequent input tests.
    late->active=false;lv_obj_set_hidden(late->icon_cont,true);lv_obj_set_hidden(late->label,true);
    mainbar_finalize_menu_pages();
    mainbar_jump_to_tilenumber(app_tile_get_tile_num(),LV_ANIM_OFF);pump();save(argv[4],"apps");
    unsigned before=clicks;click(app_entry[0].icon_img);assert(clicks==before+1&&active()==destination);mainbar_jump_back();assert(active()==app_tile_get_tile_num());
    mainbar_jump_to_tilenumber(setup_get_tile_num(),LV_ANIM_OFF);pump();save(argv[4],"setup");
    before=clicks;click(setup_entry[0].icon_img);assert(clicks==before+1&&active()==destination);mainbar_jump_back();assert(active()==setup_get_tile_num());
    check_icon_targets(&app_entry[0],app_tile_get_tile_num());check_icon_targets(&setup_entry[0],setup_get_tile_num());
    late->active=true;lv_obj_set_hidden(late->icon_cont,false);lv_obj_set_hidden(late->label,false);
    mainbar_jump_to_maintile(LV_ANIM_OFF);mainbar_finalize_menu_pages();
    std::vector<unsigned> root_pages={0};
    for(unsigned i=0;i<app_tile_get_used_pages();i++)root_pages.push_back(app_tile_num[i]);
    for(unsigned i=0;i<setup_tile_get_used_pages();i++)root_pages.push_back(setup_tile_num[i]);
    root_pages.push_back(notes_number);
    for(unsigned x=0;x<root_pages.size();x++){assert(tile_pos_table[root_pages[x]].x==x&&tile_pos_table[root_pages[x]].y==0);}
    expected_click_obj=late->icon_img;check_icon_targets(late,late_page);expected_click_obj=nullptr;
    mainbar_jump_to_maintile(LV_ANIM_OFF);pump();
    for(unsigned i=1;i<root_pages.size();i++){swipe(-1);if(active()!=root_pages[i])fprintf(stderr,"Root swipe %u reached %u\n",root_pages[i],active());assert(active()==root_pages[i]);}
    swipe(-1);assert(active()==notes_number);swipe_vertical();assert(active()==notes_number);
    for(unsigned i=root_pages.size()-1;i>0;i--){swipe(1);assert(active()==root_pages[i-1]);}
    for(unsigned i=1;i<root_pages.size();i++){mainbar_button_event_cb(BUTTON_RIGHT,nullptr);assert(active()==root_pages[i]);}
    for(unsigned i=root_pages.size()-1;i>0;i--){mainbar_button_event_cb(BUTTON_LEFT,nullptr);assert(active()==root_pages[i-1]);}
    mainbar_jump_to_tilenumber(destination,LV_ANIM_OFF);mainbar_clear_history();pump();
    mainbar_add_tile_hibernate_cb(destination,hibernated);mainbar_add_tile_activate_cb(destination+1,activated);
    for(unsigned i=1;i<4;i++){swipe(-1);assert(active()==destination+i);}
    assert(activations==1&&hibernations==1);
    swipe(-1);assert(active()==destination+3);swipe_vertical();assert(active()==destination+3);
    for(unsigned i=3;i>0;i--){swipe(1);assert(active()==destination+i-1);}
    mainbar_jump_to_maintile(LV_ANIM_OFF);mainbar_jump_to_tilenumber(destination+3,LV_ANIM_OFF);assert(active()==destination+3);mainbar_jump_back();assert(active()==0);
    for(unsigned i=0;i<40;i++){mainbar_jump_to_tilenumber(destination+(i%2),LV_ANIM_OFF);assert(active()==destination+(i%2));assert(mainbar_history->entrys<MAINBAR_MAX_HISTORY);}
    mainbar_jump_to_maintile(LV_ANIM_OFF);
    // A runtime registration crosses an occupied-page boundary while an app
    // is open and Back points at Setup. No explicit finalizer is called here.
    unsigned per_page=MAX_APPS_ICON_HORZ*MAX_APPS_ICON_VERT;
    unsigned occupied=app_tile_get_used_pages();
    while(unsigned(app_tile_get_active_app_entrys())<occupied*per_page)
        assert(app_register("Runtime filler",&message_64px,open_demo));
    unsigned setup_id=setup_get_tile_num();mainbar_jump_to_tilenumber(setup_id,LV_ANIM_OFF);mainbar_clear_history();
    mainbar_jump_to_tilenumber(destination,LV_ANIM_OFF);assert(mainbar_history->entrys==1);
    lv_point_t previous_setup=tile_pos_table[setup_id],previous_app=tile_pos_table[destination];
    unsigned prior_activations=activations,prior_hibernations=hibernations;
    assert(app_register("Runtime new page",&message_64px,open_demo));pump();
    assert(app_tile_get_used_pages()==occupied+1);assert(active()==destination);
    assert(tile_pos_table[destination].x==previous_app.x&&tile_pos_table[destination].y==previous_app.y);
    assert(tile_pos_table[setup_id].x==previous_setup.x+1&&tile_pos_table[setup_id].y==0);
    assert(mainbar_history->entrys==1&&mainbar_history->tile[1].x==tile_pos_table[setup_id].x&&mainbar_history->tile[1].y==0);
    assert(activations==prior_activations&&hibernations==prior_hibernations);
    mainbar_jump_back();assert(active()==setup_id);swipe(1);assert(active()==app_tile_num[occupied]);
    mainbar_jump_to_maintile(LV_ANIM_OFF);
    unsigned entry_count=tile_entrys;assert(mainbar_add_app_tile(0,2,"invalid")==uint32_t(-1));assert(mainbar_add_app_tile(65535,65535,"invalid")==uint32_t(-1));assert(tile_entrys==entry_count);
    // Exercise native-coordinate boundary without allocating hundreds of
    // unrelated fixture screens. Existing tile IDs remain untouched.
    app_tile_x_pos=32000/width-1;unsigned old_row=app_tile_y_pos;
    unsigned wrapped=mainbar_add_setup_tile(1,3,"wrapped group");assert(wrapped==entry_count);assert(tile_pos_table[wrapped].x==0);assert(tile_pos_table[wrapped].y==old_row+2*MAINBAR_APP_TILE_Y_START);
    for(unsigned i=1;i<3;i++){assert(tile_pos_table[wrapped+i].x==i&&tile_pos_table[wrapped+i].y==tile_pos_table[wrapped].y);}
    puts("PASS actual mainbar horizontal root/submenu swipe, icon pointer, stable IDs, lifecycle and jump/back");
}
#endif
