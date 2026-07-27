#!/usr/bin/env python3
"""Structural regression checks for the active watch builds.

These checks intentionally stay dependency-free so they can run as a
PlatformIO pre-build script and in GitHub Actions before every firmware build.
They guard the recent watch fixes that are easy to break during UI, GPS, and
power-management edits.
"""

from pathlib import Path
import sys


if "__file__" in globals():
    REPO_ROOT = Path(__file__).resolve().parents[1]
else:
    REPO_ROOT = Path.cwd()


class RegressionFailure(Exception):
    pass


def read_file(relative_path):
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8", errors="replace")


def require_tokens(relative_path, description, tokens):
    text = read_file(relative_path)
    missing = [token for token in tokens if token not in text]
    if missing:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; missing: {', '.join(missing)}"
        )
    print(f"[regression] ok: {description}")


def require_any(relative_path, description, token_groups):
    text = read_file(relative_path)
    for tokens in token_groups:
        if all(token in text for token in tokens):
            print(f"[regression] ok: {description}")
            return
    raise RegressionFailure(f"{description} regressed in {relative_path}")


def require_slice_tokens(relative_path, description, start_token, end_token, tokens):
    text = read_file(relative_path)
    start = text.find(start_token)
    if start == -1:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; missing start token: {start_token}"
        )
    end = text.find(end_token, start + len(start_token))
    if end == -1:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; missing end token: {end_token}"
        )
    slice_text = text[start:end]
    missing = [token for token in tokens if token not in slice_text]
    if missing:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; missing: {', '.join(missing)}"
        )
    print(f"[regression] ok: {description}")


def forbid_tokens(relative_path, description, tokens):
    text = read_file(relative_path)
    present = [token for token in tokens if token in text]
    if present:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; forbidden: {', '.join(present)}"
        )
    print(f"[regression] ok: {description}")


def forbid_slice_tokens(relative_path, description, start_token, end_token, tokens):
    text = read_file(relative_path)
    start = text.find(start_token)
    if start == -1:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; missing start token: {start_token}"
        )
    end = text.find(end_token, start + len(start_token))
    if end == -1:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; missing end token: {end_token}"
        )
    slice_text = text[start:end]
    present = [token for token in tokens if token in slice_text]
    if present:
        raise RegressionFailure(
            f"{description} regressed in {relative_path}; forbidden: {', '.join(present)}"
        )
    print(f"[regression] ok: {description}")


def run_checks():
    require_tokens(
        "src/gui/widget_factory.cpp",
        "Ultra shared image buttons fire immediately and suppress duplicate click events",
        [
            "wf_fast_image_button_event_cb",
            "LV_EVENT_PRESSED",
            "event_cb( obj, LV_EVENT_PRESSED );",
            "event_cb( obj, LV_EVENT_CLICKED );",
            "case LV_EVENT_CLICKED:",
            "case LV_EVENT_SHORT_CLICKED:",
            "last_press_ms",
            "lv_obj_set_ext_click_area( button, 12, 12, 12, 12 );",
        ],
    )

    for relative_path, description in [
        ("src/gui/mainbar/app_tile/xnode_sos/xnode_sos.cpp", "SOS page large press targets"),
        (
            "src/gui/mainbar/app_tile/xnode_checkin/xnode_checkin.cpp",
            "Check-In page large press targets",
        ),
        (
            "src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp",
            "Bluetooth message page large press targets",
        ),
        ("src/app/meshtastic/meshtastic_app.cpp", "Meshtastic page large exit target"),
    ]:
        require_tokens(
            relative_path,
            description,
            [
                "LV_EVENT_PRESSED",
                "target_size = 78",
                "lv_obj_set_ext_click_area",
            ],
        )

    require_tokens(
        "src/gui/mainbar/app_tile/xnode_notifications/xnode_notifications.cpp",
        "Alert Summary page responds on press",
        [
            "xnode_notifications_accept_button_event",
            "LV_EVENT_PRESSED",
            "xnode_notifications_configure_controls",
        ],
    )
    require_tokens(
        "src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp",
        "message main-tile shortcut disappears after messages are viewed",
        [
            "static bool bluetooth_message_new = false;",
            "bluetooth_message_dismiss_main_widget",
            "bluetooth_message_mark_read",
            "messages_widget = widget_remove( messages_widget );",
            "bluetooth_message_open_latest",
            "bluetooth_message_mark_read();",
            "bluetooth_message_show_msg( msg_chain_get_entrys( bluetooth_msg_chain ) - 1 );",
            "if ( bluetooth_message_new )",
            "messages_widget = widget_register( \"message\", &message_64px, enter_bluetooth_messages_cb );",
            "bluetooth_message_new = true;",
            "bluetooth_message_set_indicator();",
        ],
    )
    forbid_slice_tokens(
        "src/gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.cpp",
        "message receive does not force-open the message screen",
        "bool bluetooth_message_queue_msg( const char *msg ) {",
        "int32_t bluetooth_get_number_of_msg( void ) {",
        [
            "mainbar_jump_to_tilenumber( bluetooth_message_tile_num",
            "blectl_get_show_notification()",
        ],
    )

    require_tokens(
        "src/gui/keyboard.cpp",
        "watch keyboard paging and space handling",
        [
            "KB_PAGE_AM",
            "KB_PAGE_NZ",
            "KB_PAGE_CAP_AM",
            "KB_PAGE_CAP_NZ",
            "KB_PAGE_SYMBOL",
            "keyboard_set_page( KB_PAGE_AM );",
            "keyboard_set_page( kb_page == KB_PAGE_CAP_AM ? KB_PAGE_CAP_NZ : KB_PAGE_NZ );",
            "keyboard_set_page( KB_PAGE_SYMBOL );",
            'strcmp( txt, "space" ) == 0',
            "lv_textarea_add_char( target, ' ' );",
            "lv_textarea_del_char( target );",
        ],
    )

    require_tokens(
        "src/app/osmmap/osmmap_app_main.cpp",
        "Tac map GPS user marker renders as topmost watch/T-Deck triangle",
        [
            "OSMMAP_USE_LOCAL_TRIANGLE_MARKER",
            "defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )",
            "OSMMAP_LOCAL_MARKER_SIZE",
            "osmmap_create_local_position_marker",
            "osmmap_prepare_local_marker_image",
            "osmmap_app_pos_img",
            "lv_img_set_src( osmmap_app_pos_img, &osmmap_local_marker_image );",
            "lv_img_set_pivot( osmmap_app_pos_img, OSMMAP_LOCAL_MARKER_SIZE / 2, OSMMAP_LOCAL_MARKER_SIZE / 2 );",
            "osmmap_raise_overlay_layer",
            "lv_obj_move_foreground( osmmap_overlay_layer );",
            "osmmap_raise_local_position_marker",
            "lv_obj_move_foreground( osmmap_app_pos_img );",
            "osmmap_raise_primary_controls();",
            "osmmap_refresh_local_position_marker",
            "osmmap_set_local_position_from_gps",
            "osmmap_set_local_heading_from_gps",
            "osmmap_update_local_position_marker_heading",
        ],
    )
    require_tokens(
        "src/app/osmmap/osmmap_app_main.cpp",
        "Tac map controls keep Ultra-sized press targets and active performance mode",
        [
            "osmmap_configure_primary_control",
            "target_size = 78",
            "lv_obj_set_ext_click_area( button, 18, 18, 18, 18 );",
            "osmmap_accept_primary_control_event",
            "LV_EVENT_PRESSED",
            "powermgm_set_perf_mode();",
            "powermgm_set_normal_mode();",
            "osmmap_control_icon_color = LV_COLOR_MAKE( 0xff, 0xd2, 0x00 );",
            "lv_style_set_image_recolor( &osmmap_app_btn_style, LV_OBJ_PART_MAIN, osmmap_control_icon_color );",
            "lv_style_set_image_recolor_opa( &osmmap_app_btn_style, LV_OBJ_PART_MAIN, LV_OPA_COVER );",
        ],
    )
    require_tokens(
        "src/app/osmmap/osmmap_app_main.cpp",
        "Tac map GPS remains automatic while WiFi auto-start defaults off on Ultra",
        [
            "OSMMAP_CONFIG_VERSION",
            "osmmap_config.wifi_autoon = false;",
            "osmmap_config.gps_autoon",
            "gpsctl_on();",
        ],
    )
    require_tokens(
        "src/app/osmmap/osmmap_app_main.cpp",
        "Tac map touch pan ignores marker hit targets",
        [
            "osmmap_touch_pan_consuming",
            "osmmap_touch_starts_on_foreground_control",
            "osmmap_handle_touch_pan( const touch_t *touch )",
            "handled = osmmap_handle_touch_pan( (const touch_t *)arg );",
            "return( handled );",
            "osmmap_adjust_watch_flash_pan( dx, dy );",
            "osm_map_nav_direction( osmmap_location, total_dx > 0 ? west : east );",
            "lv_obj_set_click( item->marker_label_obj, false );",
            "lv_obj_set_click( osmmap_app_pos_img, false );",
            "lv_obj_set_click( osmmap_ext_pos_img, false );",
            "lv_obj_set_click( osmmap_north_btn, false );",
            "lv_obj_set_click( osmmap_zoom_southeast_btn, false );",
            "osmmap_reset_touch_pan();",
        ],
    )
    require_tokens(
        "src/app/osmmap/osmmap_app_main.cpp",
        "T-Deck Plus tac map zooms inside the full wide viewport",
        [
            "#elif defined( LILYGO_T_DECK_PLUS )",
            "return( width > 0 ? (uint16_t)width : 320 );",
            "return( height > 0 ? (uint16_t)height : 240 );",
            "defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )",
            "( dest_w * 0.5 ) + ( pixel_x - 128.0 )",
            "( dest_h * 0.5 ) + ( pixel_y - 128.0 )",
            "osmmap_get_tdeck_fit_height_lvgl_zoom",
            "fmin( view_w, view_h )",
            "base_lvgl_zoom * zoom_factor",
        ],
    )
    forbid_slice_tokens(
        "src/app/osmmap/osmmap_app_main.cpp",
        "T-Deck Plus tac map keeps square side bars at base zoom",
        "static uint16_t osmmap_get_watch_flash_lvgl_zoom( void ) {",
        "static uint16_t osmmap_get_display_lvgl_zoom( void ) {",
        [
            "LILYGO_T_DECK_PLUS",
        ],
    )

    require_tokens(
        "src/hardware/gpsctl.cpp",
        "Ultra GPS pins, debug path, and automatic location defaults",
        [
            "GPSCTL_CONFIG_VERSION",
            "#if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )",
            "gpsctl_config.autoon = true;",
            "gpsctl_config.app_use_gps = true;",
            "gpsctl_config.enable_on_standby = false;",
            "SHIELD_GPS_RX",
            "SHIELD_GPS_TX",
            "powermgm_set_perf_mode();",
            "WATCH_POWER_GPS",
            "WATCH_POWER_GPS_DC_CHANNEL",
        ],
    )
    require_tokens(
        "src/hardware/pmu.cpp",
        "Ultra PMU starts GPS rail off for idle battery life",
        [
            "WATCH_POWER_GPS",
            "false",
        ],
    )
    require_tokens(
        "src/hardware/twatch_ultra_hal.cpp",
        "Ultra display sleep and wake are wired into standby",
        [
            "display.sleep();",
            "display.wakeup();",
            "WATCH_POWER_GPS",
            "WATCH_POWER_GPS_DC_CHANNEL",
        ],
    )
    require_tokens(
        "src/hardware/config/gpsctlconfig.cpp",
        "Ultra and T-Deck Plus GPS config defaults stay automatic and app-usable",
        [
            "#if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )",
            'autoon = doc["autoon"] | true;',
            'app_use_gps = doc["app_use_gps"] | true;',
            "autoon = true;",
            "app_use_gps = true;",
        ],
    )
    require_tokens(
        "src/hardware/twatch_ultra_hal.cpp",
        "Ultra haptic feedback uses the real DRV2605 path",
        [
            "DRV2605_ADDR = 0x5A",
            "powerIoctl(WATCH_POWER_DRV2605, true);",
            "haptic.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, DRV2605_ADDR)",
            "haptic.setMode(DRV2605_MODE_INTTRIG);",
            "haptic.run();",
            "powerIoctl(WATCH_POWER_DRV2605, false);",
        ],
    )
    require_tokens(
        "src/hardware/motor.cpp",
        "watch haptic routing stays target-specific",
        [
            "defined( LILYGO_WATCH_ULTRA )",
            "watch.vibrate();",
            "defined( LILYGO_WATCH_S3 )",
            "watch.run();",
            "defined( LILYGO_T_DECK_PLUS )",
            "no onboard vibration motor",
        ],
    )
    require_tokens(
        "src/hardware/wifictl.cpp",
        "Ultra WiFi idle defaults stay off",
        [
            "WIFICTL_CONFIG_VERSION",
            "wifictl_config->autoon = false;",
            "wifictl_config->enable_on_standby = false;",
        ],
    )
    require_tokens(
        "src/hardware/wifictl.cpp",
        "T-Deck Plus WiFi stays lazy and skips dummy startup scan",
        [
            "#if defined( LILYGO_T_DECK_PLUS )",
            "wifictl_config->autoon = false;",
            "wifictl_config->webserver = false;",
            "wifictl_config->ftpserver = false;",
            "#if !defined( LILYGO_WATCH_ULTRA ) && !defined( LILYGO_T_DECK_PLUS )",
        ],
    )
    require_tokens(
        "src/hardware/blectl.cpp",
        "Ultra BLE advertises XNODE by default while standby blockers stay off",
        [
            "BLECTL_CONFIG_VERSION",
            "#if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )",
            "blectl_config.autoon = true;",
            "blectl_config.advertising = true;",
            "blectl_config.enable_on_standby = false;",
            "blectl_config.disable_only_disconnected = false;",
            "blectl_init_stack();",
        ],
    )
    require_slice_tokens(
        "src/hardware/blectl.cpp",
        "NimBLE lifecycle callbacks resume power management from task context",
        "class ServerCallbacks: public NimBLEServerCallbacks",
        "static void blectl_init_stack",
        [
            "powermgm_resume();",
        ],
    )
    forbid_slice_tokens(
        "src/hardware/blectl.cpp",
        "NimBLE lifecycle callbacks never use the ISR-only task resume API",
        "class ServerCallbacks: public NimBLEServerCallbacks",
        "static void blectl_init_stack",
        [
            "powermgm_resume_from_ISR();",
        ],
    )
    require_tokens(
        "src/hardware/config/blectlconfig.cpp",
        "Ultra and T-Deck Plus BLE defaults keep XNODE visible",
        [
            "static bool blectl_default_autoon( void )",
            "return true;",
            "static bool blectl_default_advertising( void )",
            "config_version = doc[\"config_version\"] | 0;",
            "if ( config_version > BLECTL_CONFIG_VERSION )",
        ],
    )
    require_tokens(
        "src/hardware/ble/meshtastic_ble.cpp",
        "Meshtastic BLE advertising preserves the XNODE bridge service UUID",
        [
            "#include \"hardware/ble/xnode.h\"",
            "advertising->reset();",
            "advertising->addServiceUUID( NimBLEUUID( xnode_ble_service_uuid() ) );",
            "advertising->addServiceUUID( MESHTASTIC_BLE_SERVICE_UUID );",
        ],
    )

    require_tokens(
        "src/hardware/motion.cpp",
        "Ultra pedometer preserves step continuity across raw counter resets",
        [
            "bma_ultra_last_counter",
            "bma_ultra_counter_base",
            "bma_ultra_step_counter_cb",
            "bma_ultra_configure_stepcounter",
            "bma_ultra_update_stepcounter",
            "raw_counter < bma_ultra_last_counter",
        ],
    )

    require_tokens(
        "src/gui/mainbar/main_tile/main_tile.cpp",
        "Ultra main screen moon phase indicator",
        [
            "MAIN_TILE_HAS_MOON",
            "LILYGO_T_DECK_PLUS",
            "MAIN_TILE_MOON_CANVAS_SIZE",
            "moon_canvas",
            "main_tile_ultra_moon_fraction",
            "known_new_moon_epoch",
            "synodic_month_seconds",
            "main_tile_ultra_draw_moon",
            "main_tile_ultra_update_moon",
            "Moon: %s %d%%",
            "LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED",
        ],
    )
    require_tokens(
        "src/app/compass/compass_app.cpp",
        "Compass app stays registered on all active targets",
        [
            'app_register( "compass"',
            "mainbar_add_app_tile( 1, 1, \"compass\" );",
            "compass_app_main_setup( compass_app_main_tile_num );",
        ],
    )
    forbid_tokens(
        "src/app/compass/compass_app.cpp",
        "Compass app is not hidden when a target lacks a magnetometer",
        [
            "if( !compass_available() )",
            "if ( !compass_available() )",
        ],
    )
    require_tokens(
        "src/app/compass/compass_app_main.cpp",
        "Compass page uses rotating rose with GPS-course fallback",
        [
            "compass_app_main_draw_rose",
            "compass_app_main_draw_tick",
            "compass_app_main_draw_cardinal",
            "compass_app_main_draw_fixed_pointer",
            "GPSCTL_UPDATE_COURSE",
            "gpsctl_register_cb( GPSCTL_UPDATE_COURSE",
            "compass_available()",
            "compass_app_main_set_heading",
            "\"GPS\"",
            "\"MAG\"",
            "display_get_timeout()",
            "display_set_timeout( DISPLAY_NO_TIMEOUT );",
            "display_trigger_activity();",
            "display_set_timeout( compass_display_timeout );",
            "GPS course needs movement",
            "LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED",
        ],
    )

    require_tokens(
        "src/hardware/motion.cpp",
        "T-Deck Plus does not leak bogus pedometer values",
        [
            "LILYGO_T_DECK_PLUS",
            "return 0;",
            "stepcounter = 0;",
            "stepcounter_before_reset = 0;",
        ],
    )
    require_tokens(
        "src/hardware/config/bmaconfig.cpp",
        "T-Deck Plus motion defaults stay disabled",
        [
            "LILYGO_T_DECK_PLUS",
            "const bool default_motion_enabled = false;",
            "enable[ BMA_STEPCOUNTER ] = false;",
            "enable[ BMA_DOUBLECLICK ] = false;",
        ],
    )
    require_tokens(
        "src/gui/statusbar.cpp",
        "T-Deck handhelds hide the unsupported step counter",
        [
            "LILYGO_T_DECK_PLUS",
            "LILYGO_T_DECK_PRO",
            "lv_obj_set_hidden( statusbar_stepcounterlabel, true );",
            "#if !defined( LILYGO_T_DECK_PLUS ) && !defined( LILYGO_T_DECK_PRO )",
            "bma_register_cb( BMACTL_STEPCOUNTER, statusbar_bmactl_event_cb, \"statusbar stepcounter\" );",
        ],
    )
    require_tokens(
        "src/gui/widget_styles.cpp",
        "T-Deck Pro keeps UI icons black on white",
        [
            "lv_style_set_bg_color( &app_icon_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_image_recolor( &app_icon_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_bg_color( &img_button_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_image_recolor( &img_button_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
        ],
    )
    require_tokens(
        "support/tdeck-pro-libdeps/HynTouch/src/hyn_touch.cpp",
        "T-Deck Pro consumes touch reports on interrupts like the factory demo",
        [
            "if (touch_press_flag)",
            "touch_press_flag = false;",
            "return hyn_data->rp_buf.rep_num;",
            "consume one controller report for",
        ],
    )
    require_tokens(
        "support/tdeck-pro-libdeps/HynTouch/src/hyn_cst66xx.c",
        "T-Deck Pro CST66xx release frames clear the prior touch",
        [
            "hyn_66xxdata->rp_buf.rep_num = 0;",
            "A zero-finger/release frame",
        ],
    )
    require_tokens(
        "src/gui/app.cpp",
        "T-Deck Pro launcher callbacks use the full LVGL icon cell",
        [
            "lv_obj_set_click( app->icon_img, false );",
            "lv_obj_set_click( app->icon_cont, true );",
            "lv_obj_set_event_cb( app->icon_cont, event_cb );",
        ],
    )
    require_tokens(
        "src/hardware/touch.cpp",
        "T-Deck Pro LVGL press survives gaps between IRQ reports",
        [
            "report_quiet_release_ms = 140",
            "press_latched && millis() - last_report_ms < report_quiet_release_ms",
            "A missing frame between",
        ],
    )
    require_tokens(
        "src/hardware/touch.cpp",
        "T-Deck Pro swipes suppress destination-page clicks until release",
        [
            "swipe_threshold = 54",
            "swipe_min_duration_ms = 70",
            "touch_swipe_suppress_click = true;",
            "if ( touch_swipe_suppress_click )",
            "data->state = LV_INDEV_STATE_REL;",
        ],
    )
    require_tokens(
        "src/hardware/gpsctl.cpp",
        "T-Deck Pro GPS uses its powered 38400-baud Serial1 receiver",
        [
            '#include "hardware/tdeck_pro_hal.h"',
            "defined( LILYGO_T_DECK_PRO )",
            "gps_serial = &Serial1;",
            "gpsctl_config.RXPin = SHIELD_GPS_RX;",
            "watch.powerIoctl( WATCH_POWER_GPS, true );",
        ],
    )
    require_tokens(
        "src/app/gps_status/gps_status_main.cpp",
        "T-Deck Pro GPS status values use a wider right column",
        [
            "#if defined( LILYGO_T_DECK_PRO )",
            "lv_disp_get_hor_res( NULL ) - 64",
        ],
    )
    require_tokens(
        "src/hardware/touch.cpp",
        "T-Deck Pro touch press forces real haptic feedback",
        [
            "motor_vibe( 3, true );",
            "The Pro has a DRV2605",
        ],
    )
    require_slice_tokens(
        "src/gui/widget_styles.cpp",
        "light theme keeps black text on light shared surfaces",
        "case( 2 ):",
        "case( 3 ):",
        [
            "lv_style_set_bg_color( &background_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_text_color( &mainbar_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_text_color( &app_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_text_color( &app_opa_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_text_color( &setup_tile_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_text_color( &setup_header_tile_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_text_color( &label_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_text_color( &button_style, LV_STATE_DEFAULT, LV_COLOR_BLACK );",
            "styles_send_event_cb( STYLE_LIGHTMODE, (void*)NULL );",
        ],
    )
    require_slice_tokens(
        "src/gui/widget_styles.cpp",
        "dark theme keeps white text on dark shared surfaces",
        "case( 1 ):",
        "case( 2 ):",
        [
            "lv_style_set_bg_color( &background_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );",
            "lv_style_set_text_color( &mainbar_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_text_color( &app_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_text_color( &app_opa_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_text_color( &setup_tile_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_text_color( &setup_header_tile_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_text_color( &label_style, LV_OBJ_PART_MAIN, LV_COLOR_WHITE );",
            "lv_style_set_text_color( &button_style, LV_STATE_DEFAULT, LV_COLOR_WHITE );",
            "styles_send_event_cb( STYLE_DARKMODE, (void*)NULL );",
        ],
    )
    require_tokens(
        "src/gui/mainbar/setup_tile/style_settings/config/styleconfig.cpp",
        "watch theme defaults and invalid recovery stay on dark high contrast",
        [
            'theme = doc["theme"] | 1;',
            'theme_migration_version = doc["theme_migrated"].as<int>();',
            "needs_save = theme_migration_version < 2;",
            "if ( theme_migration_version < 2 && theme == 2 )",
            "theme = 1;",
            "if ( theme == 3 || theme < 0 || theme > 3 )",
            "theme = 1;",
            "needs_save = true;",
        ],
    )
    forbid_tokens(
        "src/gui/mainbar/setup_tile/style_settings/config/styleconfig.cpp",
        "watch theme config does not default or recover to the grey light theme",
        [
            'theme = doc["theme"] | 2;',
            "theme = 2;",
            'doc["theme_migrated"] = true;',
        ],
    )
    require_tokens(
        "src/gui/mainbar/setup_tile/style_settings/config/styleconfig.h",
        "watch style config constructor default stays on dark high contrast",
        [
            "#elif defined( LILYGO_T_DECK_PLUS )",
            "int theme = 1;",
            "#else",
            "int theme = 1;",
        ],
    )
    forbid_tokens(
        "src/gui/mainbar/setup_tile/style_settings/config/styleconfig.h",
        "watch style config constructor does not default to grey light theme",
        [
            "int theme = 2;",
        ],
    )
    require_tokens(
        "src/gui/mainbar/setup_tile/style_settings/config/styleconfig.cpp",
        "T-Deck Plus theme defaults and recovers to dark high contrast",
        [
            "#elif defined( LILYGO_T_DECK_PLUS )",
            'theme = doc["theme"] | 1;',
            "if ( theme < 0 || theme > 1 )",
            "theme = 1;",
        ],
    )
    require_tokens(
        "src/gui/mainbar/setup_tile/style_settings/style_settings.cpp",
        "watch theme settings keep invalid selections on dark while light remains manual",
        [
            '"E-Ink\\nE-Ink neg\\nlight"',
            "if ( style_config.theme < 0 || style_config.theme > 2 )",
            "style_config.theme = 1;",
            "uint16_t selected_theme = lv_dropdown_get_selected( obj );",
            "if ( selected_theme > 2 )",
            "selected_theme = 1;",
            "style_config.theme = selected_theme;",
        ],
    )
    require_tokens(
        "src/gui/mainbar/setup_tile/style_settings/style_settings.cpp",
        "T-Deck Plus theme settings expose only high-contrast watch-style choices",
        [
            "#if defined( LILYGO_T_DECK_PLUS )",
            '"E-Ink\\nE-Ink neg"',
            "if ( style_config.theme < 0 || style_config.theme > 1 )",
            "if ( selected_theme > 1 )",
        ],
    )
    require_tokens(
        "src/gui/widget_styles.cpp",
        "runtime theme clamp keeps watch startup on dark instead of grey light",
        [
            "if ( theme == 3 || theme < 0 || theme > 3 )",
            "theme = 1;",
        ],
    )
    require_tokens(
        "src/gui/widget_styles.cpp",
        "T-Deck Plus runtime theme clamp keeps light/invalid themes out of the main UI",
        [
            "#if defined( LILYGO_T_DECK_PLUS )",
            "if ( theme < 0 || theme > 1 )",
            "theme = 1;",
        ],
    )

    require_tokens(
        "src/app/gps_status/gps_status_main.cpp",
        "T-Deck Plus GPS status uses readable full-width diagnostics",
        [
            "GPS_STATUS_FULL_DEBUG_LAYOUT",
            "LILYGO_T_DECK_PLUS",
            "gps_status_debug_text",
            "LV_LABEL_LONG_BREAK",
            "RES_X_MAX - ( GPS_STATUS_DEBUG_X * 2 )",
            "char debug_text[640]",
            "lv_label_set_text(gps_status_debug_text, debug_text);",
            "gps_status_debug_text || gps_status_debug_rows[GPS_STATUS_DEBUG_ROW_COUNT - 1]",
            "Position:%s\\n",
            "Raw RX:%lu  last:%s\\n",
        ],
    )
    require_tokens(
        "src/app/gps_status/gps_status_main.cpp",
        "Ultra GPS status keeps restored wide row layout",
        [
            "#define GPS_STATUS_DEBUG_ROW_COUNT 12",
            "#define GPS_STATUS_DEBUG_STEP 29",
            "#if defined( LILYGO_WATCH_ULTRA )",
            "lv_style_set_text_font(&gps_status_value_style, LV_STATE_DEFAULT, &lv_font_montserrat_22);",
            "\"Fix: %s\"",
            "\"Position: %s\"",
            "\"Raw RX: %lu  last %s\"",
            "\"Time sync: %s\"",
            "gpsctl_set_enable_on_standby( true );",
        ],
    )
    forbid_slice_tokens(
        "src/app/gps_status/gps_status_main.cpp",
        "Ultra GPS status rows do not collapse into a narrow cropped column",
        "    #else\n    for ( uint8_t i = 0; i < GPS_STATUS_DEBUG_ROW_COUNT; i++ ) {",
        "    #endif\n\n    gpsctl_register_cb",
        [
            "lv_obj_set_width(gps_status_debug_rows[i]",
            "lv_label_set_long_mode(gps_status_debug_rows[i]",
        ],
    )
    require_tokens(
        "src/hardware/gpsctl.cpp",
        "T-Deck Plus GPS supports L76K and u-blox/M10 receiver init",
        [
            "LILYGO_T_DECK_PLUS",
            "gps_serial = &Serial1;",
            "gpsctl_probe_tdeck_l76k",
            "gpsctl_probe_tdeck_ublox_baud",
            "gpsctl_wait_for_tdeck_ubx",
            "gps.passedChecksum() > start_passed",
            "bool *saw_valid_nmea",
            "gpsctl_drain_serial_for( 1200, true )",
            "\"rx-no-nmea\"",
            "const uint32_t probe_bauds[] = { 38400, BOARD_GPS_BAUDRATE };",
            "gpsctl_set_probe_model( \"M10\" );",
            "$PCAS06,0*1B",
            "$PCAS04,5*1C",
            "$PCAS11,3*1E",
            "gpsctl_config.autoon = true;",
            "watch.powerIoctl( WATCH_POWER_GPS, true );",
        ],
    )
    require_tokens(
        "src/hardware/timesync.cpp",
        "T-Deck Plus clock gets the same boot-time fallback and GPS update path as Ultra",
        [
            "#if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )",
            "timesync_apply_build_time_if_needed();",
            "settimeofday( &build_now, NULL )",
            "applied build-time UTC clock fallback",
            "timesync_apply_external_time( time_t epoch_seconds )",
            "timesync_send_event_cb( TIME_SYNC_UPDATE, (void *)NULL );",
        ],
    )
    require_tokens(
        "src/hardware/display.cpp",
        "T-Deck Plus enters shallow display idle instead of pulse-count fading",
        [
            "display_update_timeout_dimmer();",
            "brightness = dest_brightness;",
            "display_tdeck_enter_idle();",
            "display_tdeck_exit_idle();",
            "powermgm_set_idle_mode();",
            "DISPLAYCTL_IDLE",
        ],
    )
    require_slice_tokens(
        "src/gui/gui.cpp",
        "T-Deck Plus display timeout pauses LVGL in shallow idle",
        "bool gui_powermgm_loop_event_cb",
        "lv_task_handler();",
        [
            "#if !defined( LILYGO_T_DECK_PLUS )",
            "display_get_inactive_time_ms()",
            "POWERMGM_STANDBY_REQUEST",
            "display_is_idle()",
            "hardware_detach_lvgl_ticker();",
            "lv_task_enable( false );",
            "touch_has_activity()",
            "delay( 50 );",
        ],
    )
    require_tokens(
        "src/hardware/gpsctl.cpp",
        "T-Deck Plus GPS shuts down temporarily during display idle",
        [
            "TDECK_GPS_IDLE_GRACE_MS",
            "display_register_cb( DISPLAYCTL_IDLE, gpsctl_display_event_cb, \"gpsctl display idle\" );",
            "gpsctl_tdeck_handle_display_idle();",
            "gpsctl_autoon_off();",
            "gpsctl_autoon_on();",
        ],
    )
    forbid_slice_tokens(
        "src/hardware/display.cpp",
        "T-Deck Plus standby does not send LCD sleep command",
        "#elif defined( LILYGO_T_DECK_PLUS )\n            watch.setBrightness( 0 );\n            brightness = 0;\n            dest_brightness = 0;",
        "#elif defined( LILYGO_WATCH_2020_V1 )",
        ["watch.displaySleep();"],
    )
    forbid_slice_tokens(
        "src/hardware/display.cpp",
        "T-Deck Plus wakeup does not depend on LCD wake command",
        "#elif defined( LILYGO_T_DECK_PLUS )\n                brightness = 0;\n                dest_brightness = display_get_brightness();",
        "#elif defined( LILYGO_WATCH_2020_V1 )",
        ["watch.displayWakeup();"],
    )
    require_tokens(
        "src/hardware/touch.cpp",
        "T-Deck Plus standby can wake from touch and hardware keyboard without ESP light sleep",
        [
            "BOARD_KEYBOARD_INT",
            "attachInterrupt( BOARD_KEYBOARD_INT, &touch_irq, FALLING );",
            "gpio_wakeup_enable( (gpio_num_t)BOARD_KEYBOARD_INT, GPIO_INTR_LOW_LEVEL );",
            "digitalRead( BOARD_KEYBOARD_INT ) == LOW",
            "retval = false;",
        ],
    )
    forbid_slice_tokens(
        "src/hardware/pmu.cpp",
        "T-Deck Plus standby keeps GPS powered for receiver continuity",
        "#elif defined( LILYGO_T_DECK_PLUS )",
        "#elif defined( LILYGO_WATCH_S3 )",
        ["watch.powerIoctl( WATCH_POWER_GPS, false );"],
    )
    forbid_tokens(
        "src/hardware/tdeck_plus_hal.cpp",
        "T-Deck Plus HAL display sleep avoids ST7789 sleep/wake commands",
        [
            "display.writecommand(0x10);",
            "display.writecommand(0x11);",
        ],
    )

    require_tokens(
        "platformio.ini",
        "regression checks and post-upload resets run on active watch builds",
        [
            "[env:t-watch2020-v3-s3]",
            "[env:t-watch-ultra]",
            "[env:tdeck-plus]",
            "[env:tdeck-pro]",
            "pre:support/regression_checks.py",
            "pre:support/twatch_ultra_asyncwebserver.py",
            "post:support/twatch_ultra_post_upload_reset.py",
            "post:support/tdeck_plus_post_upload_reset.py",
            "board_upload.after_reset = no_reset_stub",
        ],
    )
    require_tokens(
        "platformio.ini",
        "active S3 builds use portable Arduino framework include paths",
        [
            "[esp32s3_arduino_framework_includes]",
            "-I$PROJECT_PACKAGES_DIR/framework-arduinoespressif32/libraries/Update/src",
            "-I$PROJECT_PACKAGES_DIR/framework-arduinoespressif32@3.20009.0/libraries/Update/src",
            "${esp32s3_arduino_framework_includes.build_flags}",
        ],
    )
    forbid_tokens(
        "platformio.ini",
        "active build flags stay portable",
        [
            "C:/Users/Eric/.platformio",
            "C:\\Users\\Eric\\.platformio",
        ],
    )
    require_slice_tokens(
        "platformio.ini",
        "T-Deck Plus uses DIO flash mode to avoid QIO boot loops",
        "[env:tdeck-plus]",
        "build_flags =",
        [
            "board_build.flash_mode = dio",
        ],
    )
    require_tokens(
        "boards/tdeck_plus.json",
        "T-Deck Plus board file defaults to DIO flash mode",
        [
            '"flash_mode": "dio"',
        ],
    )
    require_tokens(
        "package.json",
        "npm build covers all supported firmware targets",
        [
            '"test": "python support/check_watch_overlay_persistence.py && python support/regression_checks.py"',
            "pio run -e t-watch-ultra -e t-watch2020-v3-s3 -e tdeck-plus -e tdeck-pro",
        ],
    )

def main():
    try:
        run_checks()
    except RegressionFailure as exc:
        print(f"[regression] FAIL: {exc}", file=sys.stderr)
        return 1
    print("[regression] all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
elif "Import" in globals():
    result = main()
    if result:
        raise SystemExit(result)
