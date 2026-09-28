#include "config.h"
#include "meshtastic_app.h"
#include "meshtastic_service.h"
#include "gui/app.h"
#include "gui/keyboard.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_styles.h"
#include "hardware/button.h"
#include "hardware/powermgm.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef NATIVE_64BIT
#include "utils/logging.h"
#else
#include <Arduino.h>
#endif

LV_IMG_DECLARE(message_64px);
LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_12px);
LV_FONT_DECLARE(lv_font_montserrat_22);
LV_FONT_DECLARE(Ubuntu_32px);
#if defined(LILYGO_WATCH_ULTRA) || defined(LILYGO_WATCH_S3)
static constexpr bool mesh_watch = true;
#else
static constexpr bool mesh_watch = false;
#endif
#if defined(LILYGO_WATCH_ULTRA)
static constexpr int mesh_touch = 72;
static const lv_font_t *mesh_body_font = &Ubuntu_32px;
#else
static constexpr int mesh_touch = 48;
static const lv_font_t *mesh_body_font = &lv_font_montserrat_22;
#endif
static lv_obj_t *meshtastic_compose_tile, *meshtastic_watch_channel;
static lv_obj_t *meshtastic_watch_status;
static int mesh_radio_index() { return mesh_watch ? 2 : 1; }

uint32_t meshtastic_app_tile_num;
icon_t *meshtastic_app = NULL;
static lv_obj_t *meshtastic_app_tile, *meshtastic_radio_tile;
static lv_obj_t *meshtastic_channel_dropdown, *meshtastic_input, *meshtastic_send_btn;
static lv_obj_t *meshtastic_timeline, *meshtastic_status_label;
static lv_obj_t *meshtastic_radio_label, *meshtastic_send_label;
static lv_obj_t *meshtastic_latest_btn;
static bool meshtastic_active = false;
static uint32_t meshtastic_rendered_revision = UINT32_MAX;
static int meshtastic_rendered_slot = -2;
static char meshtastic_channel_options[160] = "";
static char meshtastic_error[96] = "";
// Keep channel drafts separate. Neither drafts nor messages are persisted.
static char meshtastic_drafts[9][321] = {};
static int meshtastic_draft_slot = 8;
static uint32_t meshtastic_card_sequence[24] = {};
static int meshtastic_card_y[24] = {};
static size_t meshtastic_card_count = 0;
static void meshtastic_app_refresh(void);
static void meshtastic_render_history(bool force_bottom = false);

static lv_obj_t *mesh_label(lv_obj_t *parent, const char *text, int x, int y, int width, bool small = false) {
    lv_obj_t *label = lv_label_create(parent, NULL);
    lv_obj_add_style(label, LV_OBJ_PART_MAIN, ws_get_label_style());
    lv_obj_set_style_local_text_font(label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, mesh_watch ? (small ? &lv_font_montserrat_22 : mesh_body_font) : small ? &Ubuntu_12px : &Ubuntu_16px);
    lv_label_set_long_mode(label, LV_LABEL_LONG_BREAK);
    lv_obj_set_width(label, width);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_click(label, false);
    return label;
}

static lv_obj_t *mesh_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, lv_event_cb_t cb) {
    lv_obj_t *button = lv_btn_create(parent, NULL);
    lv_obj_add_style(button, LV_BTN_PART_MAIN, ws_get_button_style());
    lv_obj_set_style_local_radius(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 3);
    lv_obj_set_style_local_border_width(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 1);
    lv_obj_set_style_local_pad_all(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_size(button, w, h);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_event_cb(button, cb);
    lv_obj_t *label = mesh_label(button, text, 0, 0, w - 4, true);
    lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(label, button, LV_ALIGN_CENTER, 0, 0);
    return button;
}

static void mesh_back(lv_obj_t *obj, lv_event_t event) {
    if (event != LV_EVENT_CLICKED) return;
    keyboard_hide();
    if (mesh_watch && obj && lv_obj_get_parent(obj) != meshtastic_app_tile)
        mainbar_jump_to_tilenumber(meshtastic_app_tile_num + (lv_obj_get_parent(obj) == meshtastic_radio_tile ? 1 : 0), LV_ANIM_OFF, false);
    else mainbar_jump_to_maintile(LV_ANIM_OFF);
}
static void mesh_page(lv_obj_t *obj, lv_event_t event) {
    static lv_point_t origin;
    static bool dragged = false;
    lv_indev_t *input = lv_indev_get_act();
    if (input && event == LV_EVENT_PRESSED) {
        lv_indev_get_point(input, &origin);
        dragged = false;
    }
    if (input && (event == LV_EVENT_PRESSING || event == LV_EVENT_RELEASED)) {
        lv_point_t point;
        lv_indev_get_point(input, &point);
        if (LV_MATH_ABS(point.x - origin.x) > 15 || LV_MATH_ABS(point.y - origin.y) > 15) dragged = true;
    }
    if (event != LV_EVENT_CLICKED) return;
    if (dragged) return; // The horizontal gesture owns navigation after a swipe.
    keyboard_hide();
    mainbar_jump_to_tilenumber(meshtastic_app_tile_num + (lv_obj_get_parent(obj) == meshtastic_app_tile ? 1 : mesh_watch && lv_obj_get_parent(obj) == meshtastic_compose_tile ? 2 : 0), LV_ANIM_OFF, false);
}
static void mesh_latest(lv_obj_t *, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) meshtastic_render_history(true);
}
static void mesh_content(lv_obj_t *obj, lv_event_t event) {
#if !defined(LILYGO_T_DECK_PRO)
    // LVGL 7 nested pages capture drag input; route horizontal content swipes
    // explicitly, keeping vertical history scrolling inside the page.
    static lv_point_t origin;
    lv_indev_t *input = lv_indev_get_act();
    if (!input) return;
    if (event == LV_EVENT_PRESSED) lv_indev_get_point(input, &origin);
    if (event != LV_EVENT_RELEASED) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    const int dx = point.x - origin.x, dy = point.y - origin.y;
    if (LV_MATH_ABS(dx) < 54 || LV_MATH_ABS(dx) <= LV_MATH_ABS(dy)) return;
    lv_obj_t *page = lv_obj_get_parent(obj);
    const bool chat = lv_obj_get_parent(page) == meshtastic_app_tile;
    if ((chat && dx < 0) || (!chat && dx > 0)) {
        lv_indev_wait_release(input);
        keyboard_hide();
        mainbar_jump_to_tilenumber(meshtastic_app_tile_num + (chat ? 1 : mesh_watch ? 1 : 0), LV_ANIM_OFF, false);
    }
#endif
}
static void mesh_input(lv_obj_t *obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) keyboard_set_textarea(obj);
}
static void mesh_channel(lv_obj_t *obj, lv_event_t event) {
    if (event != LV_EVENT_VALUE_CHANGED) return;
    keyboard_hide();
    snprintf(meshtastic_drafts[meshtastic_draft_slot], 321, "%s", lv_textarea_get_text(meshtastic_input));
    meshtastic_service_set_active_channel(lv_dropdown_get_selected(obj));
    int slot = meshtastic_service_get_channel_slot(meshtastic_service_get_active_channel());
    meshtastic_draft_slot = slot < 0 || slot > 7 ? 8 : slot;
    lv_textarea_set_text(meshtastic_input, meshtastic_drafts[meshtastic_draft_slot]);
    meshtastic_error[0] = 0;
    meshtastic_rendered_slot = -2;
    meshtastic_app_refresh();
}
static void mesh_send(lv_obj_t *, lv_event_t event) {
    if (event != LV_EVENT_CLICKED) return;
    const char *text = lv_textarea_get_text(meshtastic_input);
    bool has_text = false;
    for (const char *p = text; *p; ++p) if (*p != ' ' && *p != '\n' && *p != '\t' && *p != '\r') has_text = true;
    if (!has_text) {
        snprintf(meshtastic_error, sizeof(meshtastic_error), "Type a message first");
    } else if (meshtastic_service_send_text(text)) {
        lv_textarea_set_text(meshtastic_input, "");
        meshtastic_error[0] = 0;
        keyboard_hide();
        meshtastic_render_history(true);
        if (mesh_watch) mainbar_jump_to_tilenumber(meshtastic_app_tile_num, LV_ANIM_OFF, false);
    } else {
        snprintf(meshtastic_error, sizeof(meshtastic_error), "Not sent: %s", meshtastic_service_get_status());
    }
    meshtastic_app_refresh();
}

static int mesh_card(const mesh_message_t &message, int y) {
    if (meshtastic_card_count < 24) {
        meshtastic_card_sequence[meshtastic_card_count] = message.sequence;
        meshtastic_card_y[meshtastic_card_count++] = y;
    }
    lv_obj_t *scroll = lv_page_get_scrollable(meshtastic_timeline);
    const int width = lv_obj_get_width(meshtastic_timeline) - 28;
    lv_obj_t *card = lv_obj_create(scroll, NULL);
    lv_obj_add_style(card, LV_OBJ_PART_MAIN, ws_get_app_opa_style());
    lv_obj_set_style_local_border_width(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_side(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, message.outgoing ? LV_BORDER_SIDE_RIGHT : LV_BORDER_SIDE_LEFT);
    lv_obj_set_style_local_border_color(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
        lv_obj_get_style_border_color(meshtastic_send_btn, LV_BTN_PART_MAIN));
#if !defined(LILYGO_T_DECK_PRO)
    lv_obj_set_style_local_bg_color(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
        lv_color_mix(lv_obj_get_style_border_color(meshtastic_send_btn, LV_BTN_PART_MAIN),
                     lv_obj_get_style_bg_color(meshtastic_app_tile, LV_OBJ_PART_MAIN), message.outgoing ? 32 : 12));
#endif
    lv_obj_set_style_local_radius(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_pad_all(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_width(card, width);
    lv_obj_set_pos(card, message.outgoing ? 20 : 2, y);
    lv_obj_set_click(card, false);
    char stamp[24] = "--:--";
    if (message.timestamp) {
        time_t epoch = message.timestamp;
        struct tm local;
        if (localtime_r(&epoch, &local)) strftime(stamp, sizeof(stamp), "%H:%M", &local);
    }
    char meta[88];
    const bool acknowledged = message.outgoing && message.status == MESH_MESSAGE_ACKNOWLEDGED;
    snprintf(meta, sizeof(meta), "%s  %s%s", message.outgoing ? "YOU" : message.sender, stamp,
        message.to_node != 0 && message.to_node != UINT32_MAX ? "  DIRECT" : "");
    lv_obj_t *sender = mesh_label(card, meta, 6, 2, width - 12 - (acknowledged ? 26 : 0), true);
    int meta_height = lv_obj_get_height(sender);
    if (acknowledged) {
        lv_obj_t *ack = mesh_label(card, LV_SYMBOL_OK, width - 28, 2, 22, true);
        lv_obj_set_style_local_text_font(ack, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_22);
        if (lv_obj_get_height(ack) > meta_height) meta_height = lv_obj_get_height(ack);
    }
    lv_obj_t *body = mesh_label(card, message.text, 6, 2 + meta_height, width - 12);
    const int bottom = lv_obj_get_y(body) + lv_obj_get_height(body) + 1;
    lv_obj_set_height(card, bottom);
    return y + bottom + 3;
}

static void meshtastic_render_history(bool force_bottom) {
    if (!meshtastic_timeline) return;
    lv_obj_t *scroll = lv_page_get_scrollable(meshtastic_timeline);
    const int old_y = lv_obj_get_y(scroll);
    const int old_height = lv_obj_get_height(scroll);
    const bool at_bottom = old_height + old_y <= lv_obj_get_height(meshtastic_timeline) + 8;
    uint32_t anchor_sequence = 0;
    int anchor_offset = 0;
    for (size_t i = 0; i < meshtastic_card_count; ++i) {
        if (meshtastic_card_y[i] > -old_y) break;
        anchor_sequence = meshtastic_card_sequence[i];
        anchor_offset = old_y + meshtastic_card_y[i];
    }
    meshtastic_card_count = 0;
    lv_page_clean(meshtastic_timeline);
    int y = 2;
    {
        const int slot = meshtastic_service_get_channel_slot(meshtastic_service_get_active_channel());
        const size_t count = slot < 0 ? 0 : meshtastic_service_get_history_count(slot);
        for (size_t i = 0; i < count; ++i) {
            mesh_message_t message;
            if (meshtastic_service_get_history_message(slot, i, &message)) y = mesh_card(message, y);
        }
        if (!count) {
            mesh_label(scroll, "No messages yet", 8, 8, lv_obj_get_width(meshtastic_timeline) - 20);
            mesh_label(scroll, mesh_watch ? "Tap WRITE to start." : "Select a channel, then compose.\nHistory is kept until restart.", 8, mesh_watch ? 48 : 29, lv_obj_get_width(meshtastic_timeline) - 20, true);
            y = 64;
        }
    }
    lv_obj_set_height(scroll, y);
    const int lowest_y = LV_MATH_MIN(0, lv_obj_get_height(meshtastic_timeline) - y - 4);
    int reading_y = old_y;
    for (size_t i = 0; i < meshtastic_card_count; ++i)
        if (meshtastic_card_sequence[i] == anchor_sequence) reading_y = anchor_offset - meshtastic_card_y[i];
    lv_obj_set_y(scroll, force_bottom || at_bottom ? lowest_y : LV_MATH_MAX(lowest_y, LV_MATH_MIN(0, reading_y)));
    lv_obj_set_hidden(meshtastic_latest_btn, force_bottom || at_bottom);
    meshtastic_rendered_revision = meshtastic_service_get_history_revision();
    meshtastic_rendered_slot = meshtastic_service_get_channel_slot(meshtastic_service_get_active_channel());
}

static void meshtastic_app_refresh(void) {
    if (!meshtastic_input) return;
    // A connected client can change the selected channel too. Preserve each
    // channel's draft when that happens, just as for the on-device selector.
    const int current_slot = meshtastic_service_get_channel_slot(meshtastic_service_get_active_channel());
    const int draft_slot = current_slot < 0 || current_slot > 7 ? 8 : current_slot;
    if (draft_slot != meshtastic_draft_slot) {
        snprintf(meshtastic_drafts[meshtastic_draft_slot], 321, "%s", lv_textarea_get_text(meshtastic_input));
        meshtastic_draft_slot = draft_slot;
        keyboard_hide();
        lv_textarea_set_text(meshtastic_input, meshtastic_drafts[draft_slot]);
    }
    char options[160] = "";
    size_t length = 0;
    for (uint8_t i = 0; i < meshtastic_service_get_channel_count(); ++i) {
        char option[48];
        snprintf(option, sizeof(option), "%s", meshtastic_service_get_channel_name(i));
        if (mesh_watch) {
            char name[32]; snprintf(name, sizeof(name), "%s", meshtastic_service_get_channel_name(i));
            uint32_t bytes = strlen(name); bool shortened = false;
            do {
                snprintf(option, sizeof(option), "%d: %s%s", meshtastic_service_get_channel_slot(i) + 1, name, shortened ? "..." : "");
                if (_lv_txt_get_width(option, strlen(option), &lv_font_montserrat_22, 0, LV_TXT_FLAG_NONE) <= lv_obj_get_width(meshtastic_channel_dropdown) - 64 || !bytes) break;
                _lv_txt_encoded_prev(name, &bytes); name[bytes] = 0; shortened = true;
            } while (true);
        }
        int n = snprintf(options + length, sizeof(options) - length, "%s%s", i ? "\n" : "", option);
        if (n < 0 || (size_t)n >= sizeof(options) - length) break;
        length += n;
    }
    if (!options[0]) snprintf(options, sizeof(options), "No channel");
    if (strcmp(options, meshtastic_channel_options)) {
        snprintf(meshtastic_channel_options, sizeof(meshtastic_channel_options), "%s", options);
        lv_dropdown_set_options(meshtastic_channel_dropdown, options);
    }
    lv_dropdown_set_selected(meshtastic_channel_dropdown, meshtastic_service_get_active_channel());
    lv_label_set_text(meshtastic_send_label, "SEND");
    lv_obj_align(meshtastic_send_label, meshtastic_send_btn, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(meshtastic_status_label, meshtastic_error[0] ? meshtastic_error : meshtastic_service_get_status());
    const int slot = meshtastic_service_get_channel_slot(meshtastic_service_get_active_channel());
    if (meshtastic_rendered_slot != slot || meshtastic_rendered_revision != meshtastic_service_get_history_revision())
        meshtastic_render_history(meshtastic_rendered_slot != slot);
    char info[640];
    char peer[88];
    if (meshtastic_service_get_last_peer()) snprintf(peer, sizeof(peer), "LAST PEER  !%08" PRIX32 "\n%d dBm / %.1f dB SNR", meshtastic_service_get_last_peer(), meshtastic_service_get_last_rssi(), meshtastic_service_get_last_snr());
    else snprintf(peer, sizeof(peer), "LAST PEER\nNo packet observed");
    snprintf(info, sizeof(info), "LOCAL NODE\n%s (%s)\n!%08" PRIX32 "\n\nRADIO\n%s\n%s / %.3f MHz\n\n%s\n\nHISTORY\nLast 24 texts across channels.\nCleared on restart. Times are local.\nCheckmark: acknowledgment received.\nNo mark: no acknowledgment yet.\n\nREPLY\nSend broadcasts to the selected\nchannel, including after a direct RX.",
        meshtastic_service_get_long_name(), meshtastic_service_get_short_name(), meshtastic_service_get_node_id(),
        meshtastic_service_get_status(), meshtastic_service_get_primary_channel_name(), meshtastic_service_get_frequency_mhz(), peer);
    if (mesh_watch) {
        char details[800];
        snprintf(details, sizeof(details), "CHANNEL %d\n%s\n\n%s\n\n%s", current_slot + 1,
            meshtastic_service_get_active_channel_name(), meshtastic_service_get_status(), info);
        lv_label_set_text(meshtastic_radio_label, details);
    } else lv_label_set_text(meshtastic_radio_label, info);
    if (mesh_watch) {
        lv_label_set_text(meshtastic_watch_channel, meshtastic_service_get_active_channel_name());
        char destination[96];
        snprintf(destination, sizeof(destination), "To: %s", meshtastic_service_get_active_channel_name());
        lv_label_set_text(meshtastic_watch_status, meshtastic_error[0] ? meshtastic_error : destination);
    }
}

static void mesh_activate(void) { meshtastic_active = true; meshtastic_app_refresh(); }
static void mesh_hibernate(void) { meshtastic_active = false; keyboard_hide(); }
static bool mesh_hardware_button(EventBits_t event, void *) {
    if (event == BUTTON_EXIT) mesh_back(NULL, LV_EVENT_CLICKED);
    return true;
}
static bool mesh_loop(EventBits_t, void *) {
    static uint32_t last = 0;
    if (meshtastic_active && millis() - last >= 500) { last = millis(); meshtastic_app_refresh(); }
    return true;
}
static bool mesh_theme(EventBits_t, void *) {
    meshtastic_render_history(false);
    return true;
}
static void enter_meshtastic_app_event_cb(lv_obj_t *, lv_event_t event) {
    if (event != LV_EVENT_CLICKED) return;
    mainbar_jump_to_tilenumber(meshtastic_app_tile_num, LV_ANIM_OFF, false);
    meshtastic_active = true;
    meshtastic_app_refresh();
    app_hide_indicator(meshtastic_app);
}
void meshtastic_app_open(void) { enter_meshtastic_app_event_cb(NULL, LV_EVENT_CLICKED); }

void meshtastic_app_setup(void) {
    meshtastic_app_tile_num = mainbar_add_app_tile(mesh_watch ? 3 : 2, 1, "mesh chat");
    meshtastic_app_tile = mainbar_get_tile_obj(meshtastic_app_tile_num);
    meshtastic_radio_tile = mainbar_get_tile_obj(meshtastic_app_tile_num + mesh_radio_index());
    meshtastic_compose_tile = mesh_watch ? mainbar_get_tile_obj(meshtastic_app_tile_num + 1) : meshtastic_app_tile;
    meshtastic_app = app_register("mesh", &message_64px, enter_meshtastic_app_event_cb);
    const int w = lv_disp_get_hor_res(NULL), h = lv_disp_get_ver_res(NULL);
    const int top = STATUSBAR_HEIGHT + 2;
    const int footer = h - (mesh_watch ? mesh_touch + 4 : 26);
    const int composer = footer - (mesh_watch ? mesh_touch + 6 : 38);
    // Watches dedicate separate pages to reading, composing and radio settings.
    const bool wide = true;
    const int timeline_top = top + (mesh_watch ? mesh_touch + 6 : 61);
    for (int i = 0; i < (mesh_watch ? 3 : 2); ++i) {
        lv_obj_t *tile = mainbar_get_tile_obj(meshtastic_app_tile_num + i);
        lv_obj_add_style(tile, LV_OBJ_PART_MAIN, ws_get_app_opa_style());
        mesh_button(tile, "<", 4, top, mesh_watch ? 64 : 32, mesh_watch ? mesh_touch : 30, mesh_back);
        lv_obj_t *title = mesh_label(tile, i == mesh_radio_index() ? "RADIO" : i ? "WRITE" : "MESH CHAT", mesh_watch ? 76 : 42, top + (mesh_watch ? 12 : 7), w - (mesh_watch ? 84 : 48), w < 300);
        if (mesh_watch && i == 1) { meshtastic_watch_status = title; lv_obj_set_style_local_text_font(title, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_22); lv_label_set_long_mode(title, LV_LABEL_LONG_SROLL_CIRC); }
        if (mesh_watch && i == 0) { meshtastic_watch_channel = title; lv_label_set_long_mode(title, LV_LABEL_LONG_DOT); }
        lv_obj_t *pager = mesh_button(tile, mesh_watch ? (i == 0 ? "WRITE >" : i == 1 ? "RADIO >" : "< CHAT") : i ? "< CHAT     RADIO" : "CHAT     RADIO >", 4, footer, w - 8, mesh_watch ? mesh_touch : 24, mesh_page);
        mainbar_add_slide_element(pager);
        mainbar_add_tile_button_cb(meshtastic_app_tile_num + i, mesh_hardware_button);
        mainbar_add_tile_activate_cb(meshtastic_app_tile_num + i, mesh_activate);
        mainbar_add_tile_hibernate_cb(meshtastic_app_tile_num + i, mesh_hibernate);
    }
    meshtastic_channel_dropdown = lv_dropdown_create(mesh_watch ? meshtastic_radio_tile : meshtastic_app_tile, NULL);
    lv_obj_add_style(meshtastic_channel_dropdown, LV_DROPDOWN_PART_MAIN, ws_get_button_style());
    lv_obj_add_style(meshtastic_channel_dropdown, LV_DROPDOWN_PART_LIST, ws_get_button_style());
    lv_obj_set_style_local_text_font(meshtastic_channel_dropdown, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, mesh_watch ? &lv_font_montserrat_22 : &Ubuntu_16px);
    lv_obj_set_style_local_pad_top(meshtastic_channel_dropdown, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, 3);
    lv_obj_set_style_local_pad_bottom(meshtastic_channel_dropdown, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, 3);
    lv_obj_set_size(meshtastic_channel_dropdown, mesh_watch ? w - 8 : w / 2, mesh_watch ? mesh_touch : 26);
    if (mesh_watch) {
        lv_obj_set_style_local_pad_top(meshtastic_channel_dropdown, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, (mesh_touch - lv_font_get_line_height(&lv_font_montserrat_22) + 1) / 2);
        lv_obj_set_style_local_pad_bottom(meshtastic_channel_dropdown, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, (mesh_touch - lv_font_get_line_height(&lv_font_montserrat_22) + 1) / 2);
        lv_obj_set_style_local_text_font(meshtastic_channel_dropdown, LV_DROPDOWN_PART_LIST, LV_STATE_DEFAULT, &lv_font_montserrat_22);
        lv_obj_set_style_local_text_line_space(meshtastic_channel_dropdown, LV_DROPDOWN_PART_LIST, LV_STATE_DEFAULT, 24);
    }
    lv_obj_set_pos(meshtastic_channel_dropdown, 4, top + (mesh_watch ? mesh_touch + 6 : 32));
    lv_dropdown_set_max_height(meshtastic_channel_dropdown, h - top - 66);
    lv_obj_set_event_cb(meshtastic_channel_dropdown, mesh_channel);
    meshtastic_status_label = mesh_label(mesh_watch ? meshtastic_radio_tile : meshtastic_app_tile, "", wide ? w / 2 + 10 : 6, top + (wide ? 43 : 65), wide ? w / 2 - 16 : w - 12, true);
    lv_label_set_long_mode(meshtastic_status_label, LV_LABEL_LONG_SROLL_CIRC);
    lv_obj_set_width(meshtastic_status_label, wide ? w / 2 - 16 : w - 12);
    if (mesh_watch) lv_obj_set_hidden(meshtastic_status_label, true); // Full radio status is in the scrollable details.
    meshtastic_timeline = lv_page_create(meshtastic_app_tile, NULL);
    lv_obj_add_style(meshtastic_timeline, LV_PAGE_PART_BG, ws_get_app_opa_style());
    lv_obj_set_style_local_border_width(meshtastic_timeline, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_pad_all(meshtastic_timeline, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_pad_all(meshtastic_timeline, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0);
    lv_page_set_scrollbar_mode(meshtastic_timeline, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_event_cb(lv_page_get_scrollable(meshtastic_timeline), mesh_content);
    lv_obj_set_size(meshtastic_timeline, w - 8, (mesh_watch ? footer : composer) - timeline_top - 3);
    lv_obj_set_pos(meshtastic_timeline, 4, timeline_top);
    meshtastic_input = lv_textarea_create(meshtastic_compose_tile, NULL);
    lv_obj_add_style(meshtastic_input, LV_OBJ_PART_MAIN, ws_get_button_style());
    lv_obj_set_style_local_text_font(meshtastic_input, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, mesh_watch ? mesh_body_font : &Ubuntu_16px);
    lv_obj_set_style_local_pad_all(meshtastic_input, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 5);
    lv_textarea_set_one_line(meshtastic_input, !mesh_watch);
    lv_textarea_set_max_length(meshtastic_input, 80);
    lv_textarea_set_text(meshtastic_input, "");
    lv_textarea_set_placeholder_text(meshtastic_input, "Message channel...");
    lv_textarea_set_cursor_hidden(meshtastic_input, true);
    lv_obj_set_size(meshtastic_input, mesh_watch ? w - 8 : w - 76, mesh_watch ? composer - top - mesh_touch - 10 : 34);
    lv_obj_set_pos(meshtastic_input, 4, mesh_watch ? top + mesh_touch + 6 : composer);
    lv_obj_set_event_cb(meshtastic_input, mesh_input);
    meshtastic_send_btn = mesh_button(meshtastic_compose_tile, "SEND", mesh_watch ? 4 : w - 68, composer, mesh_watch ? w - 8 : 64, mesh_watch ? mesh_touch : 34, mesh_send);
    meshtastic_send_label = lv_obj_get_child(meshtastic_send_btn, NULL);
    meshtastic_latest_btn = mesh_button(meshtastic_app_tile, "NEW v", mesh_watch ? w - 100 : w - 64, mesh_watch ? footer - mesh_touch - 4 : composer - 27, mesh_watch ? 96 : 54, mesh_watch ? mesh_touch : 24, mesh_latest);
    lv_obj_set_hidden(meshtastic_latest_btn, true);
    lv_obj_t *radio_page = lv_page_create(meshtastic_radio_tile, NULL);
    lv_obj_add_style(radio_page, LV_PAGE_PART_BG, ws_get_app_opa_style());
    lv_obj_set_style_local_pad_all(radio_page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_pad_all(radio_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0);
    lv_obj_set_event_cb(lv_page_get_scrollable(radio_page), mesh_content);
    lv_obj_set_size(radio_page, w - 8, footer - top - (mesh_watch ? mesh_touch * 2 + 14 : 36));
    lv_obj_set_pos(radio_page, 4, top + (mesh_watch ? mesh_touch * 2 + 12 : 34));
    meshtastic_radio_label = mesh_label(lv_page_get_scrollable(radio_page), "", 4, 4, w - 32);
    meshtastic_service_setup();
    int slot = meshtastic_service_get_channel_slot(meshtastic_service_get_active_channel());
    meshtastic_draft_slot = slot < 0 || slot > 7 ? 8 : slot;
    meshtastic_app_refresh();
    styles_register_cb(STYLE_CHANGE, mesh_theme, "mesh chat theme");
    powermgm_register_loop_cb(POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP, mesh_loop, "mesh chat loop");
}
