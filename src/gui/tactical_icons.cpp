#include "tactical_icons.h"
#include "gui/widget_styles.h"
#include "gui/mainbar/mainbar.h"
#include <ctype.h>
#include <string.h>

namespace {
enum Category { COMMS, NAVIGATION, POWER, ALERT, SYSTEM, CATEGORY_COUNT };
struct Binding { icon_t *icon; Category category; bool outline; };
Binding bindings[96] = {};
size_t binding_count = 0;
lv_style_t glyph[CATEGORY_COUNT], card[CATEGORY_COUNT];
bool initialized = false;

void activate_icon(lv_obj_t *object, lv_event_t event) {
    if (event != LV_EVENT_CLICKED && event != LV_EVENT_LONG_PRESSED) return;
    for (size_t i = 0; i < binding_count; ++i) {
        icon_t *icon = bindings[i].icon;
        if (object == icon->icon_cont || object == icon->label) {
            // QuickGLUI installs its handler after app_register. Dispatch to
            // the current image callback, retaining its original identity.
            lv_event_send(icon->icon_img, event, NULL);
            return;
        }
    }
}

Category category_for(const char *name) {
    char lower[64] = {};
    for (size_t i = 0; name && name[i] && i < sizeof(lower) - 1; ++i)
        lower[i] = (char)tolower((unsigned char)name[i]);
    if (strstr(lower, "sos") || strstr(lower, "alert") || strstr(lower, "notif")) return ALERT;
    if (strstr(lower, "mess") || strstr(lower, "mesh") || strstr(lower, "wifi") || strstr(lower, "bluetooth") || strstr(lower, "phone")) return COMMS;
    if (strstr(lower, "map") || strstr(lower, "gps") || strstr(lower, "compass") || strstr(lower, "sail") || strstr(lower, "checkin")) return NAVIGATION;
    if (strstr(lower, "power") || strstr(lower, "battery") || strstr(lower, "weather") || strstr(lower, "astro")) return POWER;
    return SYSTEM;
}

bool update_palette(EventBits_t, void *) {
    lv_color_t surface = LV_COLOR_BLACK;
    _lv_style_get_color(ws_get_mainbar_style(), LV_STYLE_BG_COLOR, &surface);
    const bool light = lv_color_brightness(surface) > 128;
    const uint32_t dark_colors[] = {0x68dce8, 0x79dbae, 0xf3c766, 0xff8b78, 0x9fbbef};
    const uint32_t light_colors[] = {0x006b7a, 0x176a47, 0x795000, 0xa42e23, 0x355c92};
    for (int i = 0; i < CATEGORY_COUNT; ++i) {
#if defined(LILYGO_T_DECK_PRO)
        const lv_color_t accent = LV_COLOR_BLACK;
        const lv_color_t panel = LV_COLOR_WHITE;
#else
        const lv_color_t accent = lv_color_hex(light ? light_colors[i] : dark_colors[i]);
        const lv_color_t panel = lv_color_mix(accent, surface, light ? 20 : 28);
#endif
        lv_style_set_image_recolor(&glyph[i], LV_STATE_DEFAULT, accent);
        // Bundled raster icons include opaque light/dark detail (envelope,
        // calculator keys, etc.). Preserve that luminance instead of turning
        // the entire opaque plate into one solid color.
#if defined(LILYGO_T_DECK_PRO)
        lv_style_set_image_recolor_opa(&glyph[i], LV_STATE_DEFAULT, LV_OPA_COVER);
#else
        lv_style_set_image_recolor_opa(&glyph[i], LV_STATE_DEFAULT, LV_OPA_50);
#endif
        lv_style_set_image_opa(&glyph[i], LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_text_color(&glyph[i], LV_STATE_DEFAULT, accent);
        lv_style_set_bg_color(&card[i], LV_STATE_DEFAULT, panel);
        lv_style_set_bg_opa(&card[i], LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_border_color(&card[i], LV_STATE_DEFAULT, accent);
        lv_style_set_border_width(&card[i], LV_STATE_DEFAULT, 1);
        lv_style_set_radius(&card[i], LV_STATE_DEFAULT, 5);
        lv_obj_report_style_mod(&glyph[i]);
        lv_obj_report_style_mod(&card[i]);
    }
    return true;
}
}

void tactical_icon_refresh(icon_t *icon) {
    if (!icon || !icon->icon_img) return;
    for (size_t i = 0; i < binding_count; ++i) {
        if (bindings[i].icon != icon) continue;
        const int category = bindings[i].category;
        lv_obj_add_style(icon->icon_img, LV_OBJ_PART_MAIN, &glyph[category]);
#if !defined(LILYGO_T_DECK_PRO)
        // These bundled outline assets are dark ink with transparent interiors.
        // Stronger tint keeps them legible without flattening detailed plates.
        if (bindings[i].outline)
            lv_obj_set_style_local_image_recolor_opa(icon->icon_img, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_90);
#endif
        lv_obj_add_style(icon->icon_cont, LV_OBJ_PART_MAIN, &card[category]);
        lv_obj_align(icon->icon_img, icon->icon_cont, LV_ALIGN_CENTER, 0, 0);
        return;
    }
}

void tactical_icon_bind(icon_t *icon, const char *name, lv_event_cb_t callback) {
    if (!icon || !icon->icon_img) return;
    if (!initialized) {
        for (int i = 0; i < CATEGORY_COUNT; ++i) {
            lv_style_init(&glyph[i]);
            lv_style_init(&card[i]);
        }
        initialized = true;
        update_palette(STYLE_CHANGE, NULL);
        styles_register_cb(STYLE_CHANGE, update_palette, "tactical menu icons");
    }
    if (binding_count >= sizeof(bindings) / sizeof(bindings[0])) {
        LV_LOG_ERROR("Tactical menu icon registry full");
        return;
    }
    bindings[binding_count++] = {icon, category_for(name), name && (strstr(name, "gps status") || strstr(name, "watchface"))};
    tactical_icon_refresh(icon);

    // Image and notification badge are decoration. The full card and its
    // sibling caption both activate the same existing destination callback.
    mainbar_add_slide_element(icon->icon_cont);
    lv_obj_set_click(icon->icon_cont, true);
    lv_obj_set_event_cb(icon->icon_img, callback);
    lv_obj_set_event_cb(icon->icon_cont, activate_icon);
    lv_obj_set_click(icon->icon_img, false);
    if (icon->icon_indicator) lv_obj_set_click(icon->icon_indicator, false);
    mainbar_add_slide_element(icon->label);
    lv_obj_set_click(icon->label, true);
    lv_obj_set_event_cb(icon->label, activate_icon);
}
