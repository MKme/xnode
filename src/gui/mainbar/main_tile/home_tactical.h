/* XNODE field chronometer. Included by main_tile.cpp so the existing clock,
 * power, radio and widget handlers remain the single sources of truth. */
#include "app/osmmap/osmmap_app.h"
#include "app/meshtastic/meshtastic_app.h"
#include "home_clock_font.h"

static lv_obj_t *home_brand = NULL;
static lv_obj_t *home_wifi = NULL;
static lv_obj_t *home_ble = NULL;
static lv_obj_t *home_clock_mode = NULL;
static lv_obj_t *home_nav[4] = {};
static lv_obj_t *home_rules[3] = {};
static lv_obj_t *home_widget_buttons[MAX_WIDGET_NUM] = {};
static lv_obj_t *home_widget_labels[MAX_WIDGET_NUM] = {};
static lv_obj_t *home_widget_badges[MAX_WIDGET_NUM] = {};
static lv_obj_t *home_widget_envelopes[MAX_WIDGET_NUM] = {};
// Functional message icon; the bundled LVGL symbol font has no envelope glyph.
static const lv_point_t home_envelope_points[] = {{0,0}, {18,0}, {18,12}, {0,12}, {0,0}, {9,7}, {18,0}};
static bool home_ready = false;
static bool home_wifi_on = false, home_wifi_connected = false;
static bool home_ble_on = false, home_ble_connected = false;
static lv_color_t home_bg, home_ink, home_accent;

static void home_tactical_layout();

static lv_obj_t *home_label(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent, NULL);
    lv_obj_reset_style_list(label, LV_OBJ_PART_MAIN);
    lv_obj_add_style(label, LV_OBJ_PART_MAIN, &datestyle);
    lv_label_set_text(label, text);
    // Gluing a label to LVGL's tileview enables drag AND click. A clickable
    // child then consumes taps without reaching its button's callback. The
    // button/container already carries swipe handling; text is display-only.
    lv_obj_set_click(label, false);
    return label;
}

static void home_nav_event(lv_obj_t *obj, lv_event_t event) {
    if (event != LV_EVENT_CLICKED) return;
    if (obj == home_nav[0]) mainbar_jump_to_tilenumber(osmmap_app_get_app_main_tile_num(), LV_ANIM_OFF, true);
    else if (obj == home_nav[1]) meshtastic_app_open();
    else if (obj == home_nav[2]) mainbar_jump_to_tilenumber(app_tile_get_tile_num(), LV_ANIM_OFF);
    else if (obj == home_nav[3]) mainbar_jump_to_tilenumber(setup_get_tile_num(), LV_ANIM_OFF);
}

static void home_widget_event(lv_obj_t *obj, lv_event_t event) {
    if (event != LV_EVENT_CLICKED) return;
    for (int i = 0; i < MAX_WIDGET_NUM; ++i) {
        if (obj == home_widget_buttons[i] && widget_entry[i].active) {
            // Forward to the original widget: message dismissal, indicators and
            // destination refresh retain their established behavior.
            lv_event_send(widget_entry[i].icon_img, LV_EVENT_CLICKED, NULL);
            return;
        }
    }
}

static void home_tactical_link_event(bool wifi, EventBits_t event) {
    bool &enabled = wifi ? home_wifi_on : home_ble_on;
    bool &connected = wifi ? home_wifi_connected : home_ble_connected;
    if (event == (wifi ? WIFICTL_ON : BLECTL_ON)) enabled = true;
    else if (event == (wifi ? WIFICTL_OFF : BLECTL_OFF)) { enabled = false; connected = false; }
    else if (event == (wifi ? WIFICTL_CONNECT : BLECTL_CONNECT)) { connected = enabled; }
    else if (event == (wifi ? WIFICTL_DISCONNECT : BLECTL_DISCONNECT)) connected = false;
    if (!home_ready) return;
    lv_label_set_text(home_wifi, !home_wifi_on ? "WI-FI OFF" : home_wifi_connected ? "WI-FI LINK" : "WI-FI IDLE");
    lv_label_set_text(home_ble, !home_ble_on ? "BLE OFF" : home_ble_connected ? "BLE LINK" : "BLE IDLE");
    home_tactical_layout();
}

static void home_tactical_style() {
    if (!home_ready) return;
    lv_color_t source_bg = LV_COLOR_BLACK;
    _lv_style_get_color(ws_get_mainbar_style(), LV_STYLE_BG_COLOR, &source_bg);
    const bool light = lv_color_brightness(source_bg) > 128;
    #if defined(LILYGO_T_DECK_PRO)
        home_bg = LV_COLOR_WHITE;
        home_ink = home_accent = LV_COLOR_BLACK;
    #else
        home_bg = light ? source_bg : WS_TACTICAL_DARK_COLOR;
        home_ink = light ? LV_COLOR_BLACK : LV_COLOR_MAKE(221, 251, 243);
        home_accent = light ? LV_COLOR_MAKE(0, 86, 74) : LV_COLOR_MAKE(91, 213, 186);
    #endif
    lv_obj_set_style_local_bg_color(main_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, home_bg);
    lv_obj_set_style_local_bg_opa(main_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_text_color(&timestyle, LV_STATE_DEFAULT, home_ink);
    lv_style_set_text_color(&datestyle, LV_STATE_DEFAULT, home_ink);
    lv_style_set_text_color(&infostyle, LV_STATE_DEFAULT, home_accent);
    lv_style_set_text_color(&tempstyle, LV_STATE_DEFAULT, home_ink);
    lv_style_set_bg_opa(&timestyle, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_style_set_bg_opa(&datestyle, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_style_set_bg_opa(&infostyle, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_style_set_bg_opa(&tempstyle, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_obj_set_style_local_text_font(home_clock_mode, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &Ubuntu_12px);
    lv_obj_set_style_local_text_font(home_wifi, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_14);
    lv_obj_set_style_local_text_font(home_ble, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_14);
    lv_obj_set_style_local_text_font(home_brand, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
        lv_disp_get_ver_res(NULL) > 360 ? &lv_font_montserrat_22 : &lv_font_montserrat_16);
    lv_obj_set_style_local_text_font(batterylabel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_16);
    for (int i = 0; i < 3; ++i) {
        lv_obj_set_style_local_bg_color(home_rules[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, home_accent);
        lv_obj_set_style_local_bg_opa(home_rules[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    }
    for (int i = 0; i < 4 + MAX_WIDGET_NUM; ++i) {
        lv_obj_t *button = i < 4 ? home_nav[i] : home_widget_buttons[i - 4];
        lv_obj_set_style_local_bg_color(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, home_bg);
        lv_obj_set_style_local_bg_opa(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_obj_set_style_local_border_color(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, home_accent);
        lv_obj_set_style_local_border_width(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
        lv_obj_set_style_local_radius(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
        lv_obj_set_style_local_bg_color(button, LV_BTN_PART_MAIN, LV_STATE_PRESSED, home_accent);
        lv_obj_set_style_local_bg_opa(button, LV_BTN_PART_MAIN, LV_STATE_PRESSED, LV_OPA_30);
    }
    lv_obj_report_style_mod(&timestyle);
    lv_obj_report_style_mod(&datestyle);
    lv_obj_report_style_mod(&infostyle);
    lv_obj_report_style_mod(&tempstyle);
}

static void home_tactical_update_clock(const tm &info) {
    if (!home_ready) return;
    char date[32];
    strftime(date, sizeof(date), "%a %d %b %Y", &info);
    for (char *p = date; *p; ++p) if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    lv_label_set_text(datelabel, date);
    time_t live_epoch;
    time(&live_epoch);
    tm live_info;
    localtime_r(&live_epoch, &live_info);
    lv_label_set_text(home_clock_mode, live_info.tm_year < 124 ? "SET CLOCK" : "");
    lv_obj_set_hidden(home_clock_mode, live_info.tm_year >= 124);
}

static void home_tactical_layout() {
    if (!home_ready) return;
    const int w = lv_disp_get_hor_res(NULL), h = lv_disp_get_ver_res(NULL);
    const bool landscape = w > h;
    const bool large = h > 360;
    const bool small = h == 240 && w == 240;
    const int top = 28; // Preserve the global pull-down status/quick-controls bar.
    const int header_h = large ? 28 : 22;
    const int links_y = top + header_h;
    const int clock_y = links_y + 24;
    const int nav_h = large ? 43 : (small ? 32 : 30);
    const int nav_rows = landscape || small ? 1 : 2;
    const int nav_y = h - 6 - nav_rows * (nav_h + 4);
    lv_obj_set_pos(clock_cont, 0, 0);
    lv_obj_set_size(clock_cont, w, h);
    lv_obj_set_style_local_bg_opa(clock_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_obj_set_style_local_border_width(clock_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_hidden(infolabel, true); // The global statusbar owns power status.
    lv_obj_set_hidden(info_cont, true);
    lv_obj_set_pos(info_cont, 0, top);
    lv_obj_set_size(info_cont, w, header_h);
    lv_obj_set_style_local_bg_opa(info_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_obj_set_style_local_border_width(info_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_pos(home_brand, 10, top + 2);
    lv_obj_align(batterylabel, info_cont, LV_ALIGN_IN_RIGHT_MID, -10, 0);
    lv_obj_align(batteryicon, batterylabel, LV_ALIGN_OUT_LEFT_MID, -7, 0);
    lv_obj_set_style_local_text_color(batteryicon, LV_IMG_PART_MAIN, LV_STATE_DEFAULT, home_ink);
    lv_obj_set_style_local_image_recolor(batteryicon, LV_IMG_PART_MAIN, LV_STATE_DEFAULT, home_ink);
    lv_obj_set_hidden(wifiicon, true);
    lv_obj_set_hidden(bluetoothicon, true);
    lv_obj_set_pos(home_wifi, 12, links_y + 5);
    lv_obj_set_pos(home_ble, w / 2 + 12, links_y + 5);
    for (int i = 0; i < 3; ++i) lv_obj_set_size(home_rules[i], w - 16, 1);
    lv_obj_set_pos(home_rules[0], 8, links_y);
    lv_obj_set_pos(home_rules[1], 8, clock_y);

    const bool compact_sensor = !large && !landscape && sensor_get_available();
    int time_h = large ? (sensor_get_available() ? 100 : 112) : (landscape ? (sensor_get_available() ? 50 : 64) : compact_sensor ? 48 : small ? 62 : 70);
    lv_obj_set_style_local_text_font(timelabel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
        large ? &home_large_font : landscape && !sensor_get_available() ? &home_landscape_font : (landscape || compact_sensor) ? &Ubuntu_48px : small ? &home_small_font : &Ubuntu_72px);
    lv_label_set_long_mode(timelabel, LV_LABEL_LONG_CROP);
    lv_label_set_align(timelabel, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_size(timelabel, landscape ? 170 : w - 16, time_h);
    lv_obj_set_pos(timelabel, 8, clock_y + (large ? 4 : 0));
    lv_label_set_long_mode(datelabel, LV_LABEL_LONG_CROP);
    lv_label_set_align(datelabel, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_width(datelabel, landscape ? 172 : w - 16);
    lv_obj_set_pos(datelabel, 8, clock_y + time_h + (large ? 5 : 0));
    lv_obj_set_style_local_text_font(datelabel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
        landscape ? &Ubuntu_12px : &Ubuntu_16px);
    lv_obj_set_pos(home_clock_mode, large ? 103 : 78, top + 6);

    int active = 0;
    for (int i = 0; i < MAX_WIDGET_NUM; ++i) if (widget_entry[i].active) ++active;
    const int widget_y = nav_y - (active ? (large ? 40 : 31) : 0) - 4;
    const int body_end = widget_y - 4;
    #if defined(MAIN_TILE_HAS_MOON)
        lv_obj_set_style_local_bg_opa(moon_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
        lv_obj_set_style_local_border_width(moon_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
        const int my = landscape ? clock_y : clock_y + time_h + 24;
        lv_obj_set_pos(moon_cont, landscape ? 180 : 8, my);
        lv_obj_set_size(moon_cont, landscape ? w - 188 : w - 16, landscape ? 83 : large ? 64 : 34);
        lv_obj_align(moon_canvas, moon_cont, LV_ALIGN_IN_LEFT_MID, 0, 0);
        lv_label_set_long_mode(moonlabel, LV_LABEL_LONG_BREAK);
        lv_obj_set_width(moonlabel, lv_obj_get_width(moon_cont) - (landscape ? 0 : large ? 77 : 43));
        lv_obj_set_style_local_text_font(moonlabel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &Ubuntu_12px);
        if (landscape) {
            lv_obj_set_pos(moon_canvas, 0, 0);
            lv_obj_set_pos(moonlabel, 0, 36);
        } else lv_obj_align(moonlabel, moon_canvas, LV_ALIGN_OUT_RIGHT_MID, 9, 0);
    #endif
    lv_obj_set_pos(home_rules[2], 8, body_end);
    lv_obj_set_hidden(templabel, !sensor_get_available());
    if (sensor_get_available()) {
        lv_obj_set_style_local_text_font(templabel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &Ubuntu_12px);
        lv_obj_align(templabel, main_cont, LV_ALIGN_IN_TOP_MID, 0, body_end - 17);
    }

    int slot = 0;
    for (int i = 0; i < MAX_WIDGET_NUM; ++i) {
        icon_t &widget = widget_entry[i];
        lv_obj_set_hidden(widget.icon_cont, true);
        lv_obj_set_hidden(home_widget_buttons[i], !widget.active);
        if (!widget.active) continue;
        const int bw = (w - 16 - (active - 1) * 4) / active;
        lv_obj_set_pos(home_widget_buttons[i], 8 + slot * (bw + 4), widget_y);
        lv_obj_set_size(home_widget_buttons[i], bw, large ? 36 : 30);
        const char *name = lv_label_get_text(widget.label);
        const bool messages = strcmp(name, "message") == 0;
        lv_label_set_text(home_widget_labels[i], messages ? (active > 1 ? "MSG" : "MESSAGES  >") : name);
        lv_label_set_long_mode(home_widget_labels[i], LV_LABEL_LONG_CROP);
        lv_obj_set_size(home_widget_labels[i], bw - 24, messages ? 20 : 16);
        lv_label_set_align(home_widget_labels[i], LV_LABEL_ALIGN_CENTER);
        lv_obj_set_style_local_text_font(home_widget_labels[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, messages ? &lv_font_montserrat_16 : &Ubuntu_12px);
        lv_obj_set_style_local_text_line_space(home_widget_labels[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, -4);
        lv_obj_align(home_widget_labels[i], home_widget_buttons[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_hidden(home_widget_envelopes[i], !messages || active > 1);
        lv_obj_align(home_widget_envelopes[i], home_widget_buttons[i], LV_ALIGN_IN_LEFT_MID, 10, 0);
        // Keep widget-specific badges and secondary readouts available.
        lv_obj_set_hidden(home_widget_badges[i], lv_obj_get_hidden(widget.icon_indicator));
        if (!lv_obj_get_hidden(widget.icon_indicator)) {
            lv_img_set_src(home_widget_badges[i], lv_img_get_src(widget.icon_indicator));
            lv_obj_align(home_widget_badges[i], home_widget_buttons[i], LV_ALIGN_IN_RIGHT_MID, -3, 0);
        }
        const char *extra = lv_label_get_text(widget.ext_label);
        if (extra && *extra) {
            wf_label_printf(home_widget_labels[i], "%s\n%s", name, extra);
            lv_obj_set_height(home_widget_labels[i], 28);
            lv_obj_align(home_widget_labels[i], home_widget_buttons[i], LV_ALIGN_CENTER, 0, 0);
        }
        #if !defined(LILYGO_T_DECK_PRO)
        const lv_color_t amber = lv_color_brightness(home_bg) > 128 ? LV_COLOR_MAKE(115, 73, 0) : LV_COLOR_MAKE(245, 202, 83);
        lv_obj_set_style_local_border_color(home_widget_buttons[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, amber);
        lv_obj_set_style_local_text_color(home_widget_labels[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, amber);
        lv_obj_set_style_local_line_color(home_widget_envelopes[i], LV_LINE_PART_MAIN, LV_STATE_DEFAULT, amber);
        #else
        lv_obj_set_style_local_line_color(home_widget_envelopes[i], LV_LINE_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        #endif
        ++slot;
    }
    const int cols = landscape ? 4 : 2;
    const int bw = (w - 16 - (cols - 1) * 4) / cols;
    for (int i = 0; i < 4; ++i) {
        lv_obj_set_hidden(home_nav[i], small && i < 2);
        const int n = small ? i - 2 : i;
        if (n < 0) continue;
        lv_obj_set_pos(home_nav[i], 8 + (n % cols) * (bw + 4), nav_y + (n / cols) * (nav_h + 4));
        lv_obj_set_size(home_nav[i], bw, nav_h);
        lv_obj_t *label = lv_obj_get_child(home_nav[i], NULL);
        lv_obj_set_style_local_text_font(label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
            large ? &lv_font_montserrat_22 : landscape ? &lv_font_montserrat_14 : &lv_font_montserrat_16);
        lv_obj_align(label, home_nav[i], LV_ALIGN_CENTER, 0, 0);
    }
}

static void home_tactical_setup() {
    if (sensor_get_available()) { home_large_source.num = 2; home_large_source.den = 3; }
    home_clock_fonts_init();
    home_brand = home_label(main_cont, "XNODE");
    home_wifi = home_label(main_cont, "WI-FI OFF");
    home_ble = home_label(main_cont, "BLE OFF");
    home_clock_mode = home_label(clock_cont, "SET CLOCK");
    for (int i = 0; i < 3; ++i) {
        home_rules[i] = mainbar_obj_create(main_cont);
        lv_obj_reset_style_list(home_rules[i], LV_OBJ_PART_MAIN);
    }
    const char *names[] = { LV_SYMBOL_GPS " MAP", LV_SYMBOL_SHUFFLE " MESH", LV_SYMBOL_LIST " APPS", LV_SYMBOL_SETTINGS " SETUP" };
    for (int i = 0; i < 4; ++i) {
        home_nav[i] = lv_btn_create(main_cont, NULL);
        lv_btn_set_layout(home_nav[i], LV_LAYOUT_OFF);
        lv_obj_reset_style_list(home_nav[i], LV_BTN_PART_MAIN);
        home_label(home_nav[i], names[i]);
        lv_obj_set_event_cb(home_nav[i], home_nav_event);
        mainbar_add_slide_element(home_nav[i]);
    }
    for (int i = 0; i < MAX_WIDGET_NUM; ++i) {
        home_widget_buttons[i] = lv_btn_create(main_cont, NULL);
        lv_btn_set_layout(home_widget_buttons[i], LV_LAYOUT_OFF);
        lv_obj_reset_style_list(home_widget_buttons[i], LV_BTN_PART_MAIN);
        home_widget_labels[i] = home_label(home_widget_buttons[i], "");
        home_widget_badges[i] = lv_img_create(home_widget_buttons[i], NULL);
        lv_obj_set_click(home_widget_badges[i], false);
        home_widget_envelopes[i] = lv_line_create(home_widget_buttons[i], NULL);
        lv_obj_set_click(home_widget_envelopes[i], false);
        lv_line_set_points(home_widget_envelopes[i], home_envelope_points, 7);
        lv_obj_set_style_local_line_width(home_widget_envelopes[i], LV_LINE_PART_MAIN, LV_STATE_DEFAULT, 2);
        lv_obj_set_event_cb(home_widget_buttons[i], home_widget_event);
        mainbar_add_slide_element(home_widget_buttons[i]);
    }
    lv_label_set_text(batterylabel, "--%");
    home_ready = true;
    home_tactical_style();
    home_tactical_layout();
}
