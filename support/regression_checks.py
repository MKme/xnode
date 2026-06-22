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
        "Tac map GPS user marker renders as topmost Ultra triangle",
        [
            "osmmap_create_local_position_marker",
            "osmmap_prepare_local_marker_image",
            "osmmap_app_pos_img",
            "osmmap_raise_local_position_marker",
            "lv_obj_move_foreground( osmmap_app_pos_img );",
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
        "src/hardware/gpsctl.cpp",
        "Ultra GPS pins, debug path, and idle power defaults",
        [
            "GPSCTL_CONFIG_VERSION",
            "gpsctl_config.autoon = false;",
            "gpsctl_config.enable_on_standby = false;",
            "SHIELD_GPS_RX",
            "SHIELD_GPS_TX",
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
        "src/hardware/wifictl.cpp",
        "Ultra WiFi idle defaults stay off",
        [
            "WIFICTL_CONFIG_VERSION",
            "wifictl_config->autoon = false;",
            "wifictl_config->enable_on_standby = false;",
        ],
    )
    require_tokens(
        "src/hardware/blectl.cpp",
        "Ultra BLE idle defaults stay off while BLE remains lazy-started",
        [
            "BLECTL_CONFIG_VERSION",
            "blectl_config.autoon = false;",
            "blectl_config.enable_on_standby = false;",
            "blectl_config.advertising = false;",
            "blectl_init_stack();",
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
        "platformio.ini",
        "regression checks run before both active watch builds",
        [
            "[env:t-watch2020-v3-s3]",
            "[env:t-watch-ultra]",
            "pre:support/regression_checks.py",
            "pre:support/twatch_ultra_asyncwebserver.py",
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
