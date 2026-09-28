// Actual Mesh Chat, native LVGL/mainbar, and actual software keyboard. Radio,
// NVS, clock and physical input are deterministic fixtures, never RF activity.
#define XNODE_MESH_HARNESS
#include "navigation.cpp"
#include <ctime>
#include <string>
#include <map>
#include <cstdarg>
#include "app/meshtastic/meshtastic_service.h"
#include "gui/keyboard.h"
void display_trigger_activity(){}
struct WatchFixture {char readKeyboardChar(){return 0;}} watch;
void keyboard_show();void num_keyboard_show();void keyboard_set_textarea(lv_obj_t*);void num_keyboard_set_textarea(lv_obj_t*);
#include "gui/keyboard.cpp"
static std::map<std::string,bool> preferences;
class Preferences {public: bool begin(const char*,bool=false){return true;}bool getBool(const char*k,bool d=false){auto i=preferences.find(k);return i==preferences.end()?d:i->second;}size_t putBool(const char*k,bool v){preferences[k]=v;return 1;}void end(){}};
void powermgm_register_loop_cb(EventBits_t,CALLBACK_FUNC,const char*){}
#include "app/meshtastic/mesh_history.cpp"
static MeshHistory history;
int8_t meshtastic_service_get_channel_slot(uint8_t n){return n<2?(n?3:0):-1;}
uint32_t meshtastic_service_get_history_revision(){return history.revision();}
size_t meshtastic_service_get_history_count(uint8_t slot){return history.count(slot);}
bool meshtastic_service_get_history_message(uint8_t slot,size_t i,mesh_message_t*out){return history.get(slot,i,out);}
static unsigned radio_calls=0, selected_channel=0, inbox_calls=0;
static bool radio_accepts=true;
static bool long_channel_names=false;
void bluetooth_message_open(){inbox_calls++;}
void meshtastic_service_setup(){}
bool meshtastic_service_send_text(const char*text){radio_calls++;if(!text||!text[0]||strlen(text)>200)return false;mesh_message_t m={};m.outgoing=true;m.status=radio_accepts?MESH_MESSAGE_QUEUED:MESH_MESSAGE_FAILED;m.channel_slot=meshtastic_service_get_channel_slot(selected_channel);m.from_node=0xabcdef01;m.timestamp=1790617200;strncpy(m.sender,"YOU",39);strncpy(m.text,text,200);history.add(m);return radio_accepts;}
bool meshtastic_service_is_ready(){return radio_accepts;}
bool meshtastic_service_is_receiving(){return false;}
const char*meshtastic_service_get_status(){return radio_accepts?"Radio ready":"Radio offline";}
uint8_t meshtastic_service_get_channel_count(){return 2;}
const char*meshtastic_service_get_channel_name(uint8_t n){return long_channel_names ? (n ? "WWWWWWWWWWWWWWW" : "MMMMMMMMMMMMMMM") : n?"Team":"LongFast";}
uint8_t meshtastic_service_get_active_channel(){return selected_channel;}
bool meshtastic_service_set_active_channel(uint8_t n){if(n>=2)return false;selected_channel=n;return true;}
const char*meshtastic_service_get_active_channel_name(){return meshtastic_service_get_channel_name(selected_channel);}
const char*meshtastic_service_get_primary_channel_name(){return "LongFast";}
float meshtastic_service_get_frequency_mhz(){return 915.0f;}
uint32_t meshtastic_service_get_node_id(){return 0xabcdef01;}
const char*meshtastic_service_get_long_name(){return "XNODE native fixture";}
const char*meshtastic_service_get_short_name(){return "XN";}
uint32_t meshtastic_service_get_last_peer(){return 0x12345678;}
int32_t meshtastic_service_get_last_rssi(){return -92;}
float meshtastic_service_get_last_snr(){return 7.5f;}
const char*meshtastic_service_get_last_message_sender(){return "ALPHA";}
const char*meshtastic_service_get_last_message_text(){return "Native fixture message";}
static tm *capture_localtime(const time_t*t,tm*out){*out=*gmtime(t);return out;}
#define localtime_r capture_localtime
#include "app/meshtastic/meshtastic_app.cpp"
static void keyboard_click(const char *label){
    auto ext=(lv_btnmatrix_ext_t*)lv_obj_get_ext_attr(kb);lv_area_t bounds;lv_obj_get_coords(kb,&bounds);
    for(unsigned i=0;i<ext->btn_cnt;i++)if(strcmp(lv_btnmatrix_get_btn_text(kb,i),label)==0){auto a=ext->button_areas[i];click_point(bounds.x1+(a.x1+a.x2)/2,bounds.y1+(a.y1+a.y2)/2);return;}
    fprintf(stderr,"Keyboard key missing: %s\n",label);assert(false);
}
static void select_channel(unsigned index){
    auto prior=active(); if(mesh_watch){mainbar_jump_to_tilenumber(meshtastic_app_tile_num+2,LV_ANIM_OFF,false);pump();}
    click(meshtastic_channel_dropdown);pump();auto ext=(lv_dropdown_ext_t*)lv_obj_get_ext_attr(meshtastic_channel_dropdown);assert(ext->page);
    auto label=lv_obj_get_child(lv_page_get_scrl(ext->page),nullptr);assert(label);lv_area_t a;lv_obj_get_coords(label,&a);
    const int line=lv_font_get_line_height(lv_obj_get_style_text_font(label,LV_LABEL_PART_MAIN))+lv_obj_get_style_text_line_space(label,LV_LABEL_PART_MAIN);
    click_point(a.x1+12,a.y1+index*line+line/2);assert(lv_dropdown_get_selected(meshtastic_channel_dropdown)==index);
    if(mesh_watch){mainbar_jump_to_tilenumber(prior,LV_ANIM_OFF,false);pump();}
}
static void drag(int x1,int y1,int x2,int y2){pointer={(lv_coord_t)x1,(lv_coord_t)y1};pointer_state=LV_INDEV_STATE_PR;pump();for(int i=1;i<=12;i++){pointer.x=x1+(x2-x1)*i/12;pointer.y=y1+(y2-y1)*i/12;pump(2);}pointer_state=LV_INDEV_STATE_REL;pump(30);}
static void incoming(const char *text,unsigned slot=0,unsigned id=1){
    mesh_message_t m={};m.from_node=0x12345678;m.to_node=0xffffffff;m.packet_id=id;m.timestamp=1790617200;m.channel_slot=slot;m.status=MESH_MESSAGE_RECEIVED;strncpy(m.sender,"ALPHA",39);strncpy(m.text,text,200);assert(history.add(m));
}
static std::pair<std::string,int> visible_anchor(){
    lv_area_t viewport;lv_obj_get_coords(meshtastic_timeline,&viewport);lv_obj_t *best=nullptr;int best_y=100000;
    auto scroll=lv_page_get_scrollable(meshtastic_timeline);
    for(auto card=lv_obj_get_child(scroll,nullptr);card;card=lv_obj_get_child(scroll,card)){lv_area_t a;lv_obj_get_coords(card,&a);if(a.y2>=viewport.y1&&a.y1<=viewport.y2&&a.y1<best_y){best=card;best_y=a.y1;}}
    assert(best);std::string body;for(auto child=lv_obj_get_child(best,nullptr);child;child=lv_obj_get_child(best,child)){const char *t=lv_label_get_text(child);if(t&&strlen(t)>body.size())body=t;}
    return {body,best_y-viewport.y1};
}
static bool tree_contains(lv_obj_t *root,const char *needle){lv_obj_type_t type;lv_obj_get_type(root,&type);if(type.type[0]&&strcmp(type.type[0],"lv_label")==0&&strstr(lv_label_get_text(root),needle))return true;for(auto child=lv_obj_get_child(root,nullptr);child;child=lv_obj_get_child(root,child))if(tree_contains(child,needle))return true;return false;}
static void check_visible(lv_obj_t *o){lv_area_t a;lv_obj_get_coords(o,&a);assert(a.x1>=0&&a.y1>=0&&a.x2<width&&a.y2<height&&!lv_obj_get_hidden(o));}
int main(int argc,char**argv){
    assert(argc==5||argc==6);bool legacy_examples_on=argc==6; if(legacy_examples_on)preferences["examples"]=true; width=atoi(argv[1]);height=atoi(argv[2]);lv_init();lv_png_init();pixels.resize(width*height*3);
    std::vector<lv_color_t> buffer(width*height);static lv_disp_buf_t buf;lv_disp_buf_init(&buf,buffer.data(),nullptr,buffer.size());lv_disp_drv_t dd;lv_disp_drv_init(&dd);dd.hor_res=width;dd.ver_res=height;dd.buffer=&buf;dd.flush_cb=flush;lv_disp_drv_register(&dd);
    lv_indev_drv_t id;lv_indev_drv_init(&id);id.type=LV_INDEV_TYPE_POINTER;id.read_cb=read_pointer;lv_indev_drv_register(&id);
    ws_get_mainbar_style();widget_style_theme_set(strcmp(argv[3],"light")==0?0:1);lv_obj_add_style(lv_scr_act(),LV_OBJ_PART_MAIN,ws_get_background_style());
    mainbar_setup();mainbar_add_tile(0,0,"home",ws_get_mainbar_style());app_tile_setup();setup_tile_setup();notes_number=mainbar_add_tile(1+MAX_APPS_TILES+MAX_SETUP_TILES,0,"notes",ws_get_mainbar_style());
    keyboard_setup();num_keyboard_setup();meshtastic_app_setup();mainbar_finalize_menu_pages();meshtastic_app_open();pump();
    assert(active()==meshtastic_app_tile_num);
    assert(!tree_contains(meshtastic_app_tile,"EXAMPLES"));
    assert(!tree_contains(meshtastic_app_tile,"PREVIEW"));
    assert(tree_contains(meshtastic_compose_tile,"SEND"));
    assert(tree_contains(meshtastic_timeline,"No messages yet"));
    assert(history.count(0)==0 && radio_calls==0);
    check_visible(meshtastic_timeline);
    if(!mesh_watch)for(auto o:{meshtastic_channel_dropdown,meshtastic_input,meshtastic_send_btn})check_visible(o);
    save(argv[4],"empty");
    if(mesh_watch){mainbar_jump_to_tilenumber(meshtastic_app_tile_num+1,LV_ANIM_OFF,false);pump();assert(lv_obj_get_height(meshtastic_send_btn)>=mesh_touch);assert(lv_obj_get_height(meshtastic_input)>=48);save(argv[4],"review");}
    lv_textarea_set_text(meshtastic_input,"Rejected draft stays");radio_accepts=false;click(meshtastic_send_btn);assert(radio_calls==1);assert(strcmp(lv_textarea_get_text(meshtastic_input),"Rejected draft stays")==0);assert(history.count(0)==1);save(argv[4],"rejected");
    radio_accepts=true;incoming("North trail clear.");meshtastic_app_refresh();click(meshtastic_send_btn);assert(radio_calls==2&&history.count(0)==3);assert(strlen(lv_textarea_get_text(meshtastic_input))==0);assert(tree_contains(meshtastic_timeline,"QUEUED"));save(argv[4],"live");mesh_message_t sent;assert(history.get(0,2,&sent));assert(history.set_status(sent.sequence,MESH_MESSAGE_TRANSMITTED));meshtastic_app_refresh();assert(tree_contains(meshtastic_timeline,"TX SENT"));save(argv[4],"transmitted");
    // Test mapped channel list index 1 => actual slot 3, with separate history.
    lv_textarea_set_text(meshtastic_input,"LongFast draft");incoming("Team channel only.",3,2);select_channel(1);assert(strlen(lv_textarea_get_text(meshtastic_input))==0);lv_textarea_set_text(meshtastic_input,"Team draft");pump();assert(selected_channel==1);save(argv[4],"channel");
    select_channel(0);assert(strcmp(lv_textarea_get_text(meshtastic_input),"LongFast draft")==0);
    selected_channel=1;meshtastic_app_refresh();assert(strcmp(lv_textarea_get_text(meshtastic_input),"Team draft")==0);selected_channel=0;meshtastic_app_refresh();assert(strcmp(lv_textarea_get_text(meshtastic_input),"LongFast draft")==0);
    if(mesh_watch){mainbar_jump_to_tilenumber(meshtastic_app_tile_num,LV_ANIM_OFF,false);pump();}
    for(unsigned i=3;i<33;i++){char text[201];snprintf(text,sizeof text,"Field report %u. A long radio message remains fully readable: north trail clear; continue to checkpoint and hold position until the next scheduled check-in. Verify route before proceeding.",i);incoming(text,0,i);}
    meshtastic_app_refresh();pump();assert(history.count(0)==MeshHistory::CAPACITY);save(argv[4],"full-history");
    auto scroll=lv_page_get_scrollable(meshtastic_timeline);int before_y=lv_obj_get_y(scroll);lv_area_t timeline_bounds;lv_obj_get_coords(meshtastic_timeline,&timeline_bounds);
    drag(width/2,timeline_bounds.y1+8,width/2,timeline_bounds.y2-8);assert(lv_obj_get_y(scroll)>before_y);int older_y=lv_obj_get_y(scroll);auto anchor_before=visible_anchor();
    incoming("A new report while reading earlier history.",0,100);meshtastic_app_refresh();pump();assert(!lv_obj_get_hidden(meshtastic_latest_btn));assert(lv_obj_get_y(scroll)>=older_y-8);auto anchor_after=visible_anchor();assert(anchor_before.first==anchor_after.first&&abs(anchor_before.second-anchor_after.second)<=1);save(argv[4],"unread");click(meshtastic_latest_btn);assert(lv_obj_get_hidden(meshtastic_latest_btn));

    // Compose through the actual keyboard implementation. Watch/Pro opens a
    // dedicated screen; Plus retains its hardware keyboard focus in-chat.
    if(mesh_watch){mainbar_jump_to_tilenumber(meshtastic_app_tile_num+1,LV_ANIM_OFF,false);pump();}
    lv_textarea_set_text(meshtastic_input,"");click(meshtastic_input);
#if defined(LILYGO_T_DECK_PLUS)
    assert(kb_user_textarea==meshtastic_input);
#else
    assert(kb_screen&&!lv_obj_get_hidden(kb_screen));assert(lv_textarea_get_max_length(kb_textarea)==80&&lv_textarea_get_one_line(kb_textarea)==!mesh_watch);check_visible(kb);check_visible(kb_textarea);
    keyboard_click("a");keyboard_click("N-Z");keyboard_click("n");keyboard_click("SPC");keyboard_click("123");keyboard_click("1");
    assert(strcmp(lv_textarea_get_text(kb_textarea),"an 1")==0);save(argv[4],"compose");
    keyboard_click(LV_SYMBOL_OK);assert(lv_obj_get_hidden(kb_screen));assert(radio_calls==2);assert(strcmp(lv_textarea_get_text(meshtastic_input),"an 1")==0);
    click(meshtastic_input);keyboard_click("b");keyboard_click(LV_SYMBOL_CLOSE);assert(strcmp(lv_textarea_get_text(meshtastic_input),"an 1")==0);
#endif
    keyboard_hide();
    if(mesh_watch){mainbar_jump_to_tilenumber(meshtastic_app_tile_num,LV_ANIM_OFF,false);pump();}
    lv_area_t swipe_area;lv_obj_get_coords(meshtastic_timeline,&swipe_area);drag(width-30,(swipe_area.y1+swipe_area.y2)/2,30,(swipe_area.y1+swipe_area.y2)/2);assert(active()==meshtastic_app_tile_num+1);
    mainbar_jump_to_tilenumber(meshtastic_app_tile_num+mesh_radio_index(),LV_ANIM_OFF,false);pump();auto radio_scroll=lv_obj_get_parent(meshtastic_radio_label);auto radio_page=lv_obj_get_parent(radio_scroll);assert(lv_obj_get_width(radio_scroll)<=lv_obj_get_width(radio_page));save(argv[4],"radio");
    lv_area_t radio_bounds;lv_obj_get_coords(radio_page,&radio_bounds);drag(30,(radio_bounds.y1+radio_bounds.y2)/2,width-30,(radio_bounds.y1+radio_bounds.y2)/2);assert(active()==meshtastic_app_tile_num+(mesh_watch?1:0));
    mainbar_jump_to_tilenumber(meshtastic_app_tile_num,LV_ANIM_OFF,false);drag(width-30,height-14,30,height-14);assert(active()==meshtastic_app_tile_num+1);
    drag(30,height-14,width-30,height-14);assert(active()==meshtastic_app_tile_num);

    mainbar_jump_to_maintile(LV_ANIM_OFF);meshtastic_app_open();assert(history.count(0)==MeshHistory::CAPACITY);assert(strcmp(lv_textarea_get_text(meshtastic_input),
#if defined(LILYGO_T_DECK_PLUS)
    ""
#else
    "an 1"
#endif
    )==0);
    assert(radio_calls==2);
    if (mesh_watch) {
        mainbar_jump_to_tilenumber(meshtastic_app_tile_num,LV_ANIM_OFF,false);pump();
        for(int page=0;page<3;++page){
            assert(active()==meshtastic_app_tile_num+page);
            auto tile=mainbar_get_tile_obj(meshtastic_app_tile_num+page);
            for(auto child=lv_obj_get_child(tile,nullptr);child;child=lv_obj_get_child(tile,child)) {
                lv_obj_type_t type;lv_obj_get_type(child,&type);
                if(type.type[0]&&strcmp(type.type[0],"lv_btn")==0) {
                    assert(lv_obj_get_width(child)>=64 && lv_obj_get_height(child)>=mesh_touch);
                }
            }
            click_point(width/2,height-4-mesh_touch/2);pump();
        }
        assert(active()==meshtastic_app_tile_num);
        // Clear only deterministic harness fixtures for representative reading captures.
        history=MeshHistory{};selected_channel=0;
        incoming("North trail clear.",0,900);
        mesh_message_t reply={};reply.outgoing=true;reply.channel_slot=0;reply.timestamp=1790617260;reply.status=MESH_MESSAGE_TRANSMITTED;
        strcpy(reply.text,"Copy. Moving out.");history.add(reply);
        meshtastic_rendered_slot=-2;meshtastic_app_refresh();pump();save(argv[4],"reading");
        mainbar_jump_to_tilenumber(meshtastic_app_tile_num+1,LV_ANIM_OFF,false);pump();
        lv_textarea_set_text(meshtastic_input,"At checkpoint. Holding here. Check north trail and report when the team is ready.");pump();save(argv[4],"draft-review");
        auto editor_scroll=lv_page_get_scrollable(meshtastic_input);int editor_y=lv_obj_get_y(editor_scroll);
        lv_area_t editor_bounds;lv_obj_get_coords(meshtastic_input,&editor_bounds);
        drag(width/2,editor_bounds.y1+6,width/2,editor_bounds.y2-6);
        assert(lv_obj_get_hidden(kb_screen));
        if(lv_obj_get_height(editor_scroll)>lv_obj_get_height(meshtastic_input))assert(lv_obj_get_y(editor_scroll)>editor_y);
        save(argv[4],"draft-review-scrolled");
        click_point(36,STATUSBAR_HEIGHT+2+mesh_touch/2);assert(active()==meshtastic_app_tile_num);
        mainbar_jump_to_tilenumber(meshtastic_app_tile_num+1,LV_ANIM_OFF,false);pump();
        assert(strstr(lv_textarea_get_text(meshtastic_input),"At checkpoint.")==lv_textarea_get_text(meshtastic_input));
        long_channel_names=true;meshtastic_app_refresh();
        mainbar_jump_to_tilenumber(meshtastic_app_tile_num+2,LV_ANIM_OFF,false);pump();
        assert(lv_obj_get_height(meshtastic_channel_dropdown)>=mesh_touch);
        click(meshtastic_channel_dropdown);pump();
        auto dropdown_ext=(lv_dropdown_ext_t*)lv_obj_get_ext_attr(meshtastic_channel_dropdown);check_visible(dropdown_ext->page);
        save(argv[4],"channel-popup");
        lv_dropdown_close(meshtastic_channel_dropdown);select_channel(1);assert(selected_channel==1);
        save(argv[4],"long-channel");
    }
    puts("PASS production live chat, legacy example preference ignored, mapped channels, send, history, keyboard and navigation");
}
