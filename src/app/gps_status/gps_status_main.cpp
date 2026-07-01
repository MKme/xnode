/****************************************************************************
 *   Apr 13 14:17:11 2021
 *   Copyright  2021  Cornelius Wild
 *   Email: tt-watch-code@dervomsee.de
 *   Based on the work of Dirk Brosswick,  sharandac / My-TTGO-Watch  Example_App"
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "config.h"

#include "gps_status.h"
#include "gps_status_main.h"

#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "hardware/gpsctl.h"
#include "hardware/display.h"
#include "gui/mainbar/mainbar.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else

#endif
/*
 * tile  and style objects
 */
lv_obj_t *gps_status_main_tile = NULL;
lv_style_t gps_status_main_style;
lv_style_t gps_status_value_style;
/*
 * objects
 */
static lv_style_t style_led_green;
static lv_style_t style_led_red;
lv_obj_t *satfix_value_on = NULL;
lv_obj_t *satfix_value_off = NULL;
lv_obj_t *num_satellites_value = NULL;
lv_obj_t *satellite_type = NULL;
lv_obj_t *pos_longlat_value = NULL;
lv_obj_t *altitude_value = NULL;
lv_obj_t *speed_value = NULL;
lv_obj_t *source_value = NULL;
lv_obj_t *gps_status_debug_rows[12] = { NULL };
lv_obj_t *gps_status_debug_text = NULL;
lv_task_t *gps_status_debug_task = NULL;
static bool gps_status_block_return_maintile = false;
static bool gps_status_prev_autoon = false;
static bool gps_status_prev_enable_on_standby = false;
static bool gps_status_forced_gps = false;
static bool gps_status_active = false;

#if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )
    #define GPS_STATUS_FULL_DEBUG_LAYOUT 1
#endif

#if defined( LILYGO_T_DECK_PLUS )
    #define GPS_STATUS_DEBUG_ROW_COUNT 10
    #define GPS_STATUS_DEBUG_X 6
    #define GPS_STATUS_DEBUG_Y ( STATUSBAR_HEIGHT + 4 )
    #define GPS_STATUS_DEBUG_STEP 18
#elif defined( LILYGO_WATCH_ULTRA )
    #define GPS_STATUS_DEBUG_ROW_COUNT 10
    #define GPS_STATUS_DEBUG_X 6
    #define GPS_STATUS_DEBUG_Y ( STATUSBAR_HEIGHT + 4 )
    #define GPS_STATUS_DEBUG_STEP 18
#else
    #define GPS_STATUS_DEBUG_ROW_COUNT 12
    #define GPS_STATUS_DEBUG_X 10
    #define GPS_STATUS_DEBUG_Y ( STATUSBAR_HEIGHT + 8 )
    #define GPS_STATUS_DEBUG_STEP 29
#endif
/*
 * images
 */
LV_IMG_DECLARE(refresh_32px);
LV_FONT_DECLARE(Ubuntu_32px);
LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(lv_font_montserrat_22);

bool style_change_event_cb( EventBits_t event, void *arg );
bool gpsctl_gps_status_event_cb( EventBits_t event, void *arg );
void gps_status_debug_task_cb(lv_task_t *task);
static void gps_status_config_value_label( lv_obj_t *label );
static void gps_status_format_age( uint32_t age_ms, char *buf, size_t len );
static void gps_status_update_debug_label( void );
void gps_status_task(lv_task_t *task);
void gps_status_hibernate_cb(void);
void gps_status_activate_cb(void);

void gps_status_main_setup(uint32_t tile_num) {
    gps_status_main_tile = mainbar_get_tile_obj(tile_num);
    lv_style_copy(&gps_status_main_style, ws_get_mainbar_style());

    lv_obj_t * exit_btn = wf_add_exit_button( gps_status_main_tile );
    lv_obj_align(exit_btn, gps_status_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_PADDING, -THEME_PADDING);

    lv_style_copy(&gps_status_value_style, ws_get_mainbar_style());
    lv_style_set_bg_color(&gps_status_value_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK);
    lv_style_set_bg_opa(&gps_status_value_style, LV_OBJ_PART_MAIN, LV_OPA_0);
    lv_style_set_border_width(&gps_status_value_style, LV_OBJ_PART_MAIN, 0);
    lv_style_set_text_font(&gps_status_value_style, LV_STATE_DEFAULT, &Ubuntu_16px);
#if defined( LILYGO_WATCH_ULTRA ) && !defined( GPS_STATUS_FULL_DEBUG_LAYOUT )
    lv_style_set_text_font(&gps_status_value_style, LV_STATE_DEFAULT, &lv_font_montserrat_22);
#endif
    /*
     * led style
     */
    lv_style_init(&style_led_green);
    lv_style_set_bg_color(&style_led_green, LV_STATE_DEFAULT, lv_color_hex(0x00d000));
    lv_style_set_border_color(&style_led_green, LV_STATE_DEFAULT, lv_color_hex(0x00d000));
    lv_style_set_shadow_color(&style_led_green, LV_STATE_DEFAULT, lv_color_hex(0x00d000));
    lv_style_set_shadow_spread(&style_led_green, LV_STATE_DEFAULT, 4);
    lv_style_init(&style_led_red);
    lv_style_set_bg_color(&style_led_red, LV_STATE_DEFAULT, lv_color_hex(0x900000));
    lv_style_set_border_color(&style_led_red, LV_STATE_DEFAULT, lv_color_hex(0x900000));
    lv_style_set_shadow_color(&style_led_red, LV_STATE_DEFAULT, lv_color_hex(0x900000));
    lv_style_set_shadow_spread(&style_led_red, LV_STATE_DEFAULT, 4);

#if defined( GPS_STATUS_FULL_DEBUG_LAYOUT )
    #if defined( LILYGO_T_DECK_PLUS )
    gps_status_debug_text = lv_label_create(gps_status_main_tile, NULL);
    lv_obj_add_style(gps_status_debug_text, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_long_mode(gps_status_debug_text, LV_LABEL_LONG_BREAK);
    lv_label_set_align(gps_status_debug_text, LV_LABEL_ALIGN_LEFT);
    lv_obj_set_size(gps_status_debug_text, RES_X_MAX - ( GPS_STATUS_DEBUG_X * 2 ), RES_Y_MAX - GPS_STATUS_DEBUG_Y - 42);
    lv_obj_align(gps_status_debug_text, gps_status_main_tile, LV_ALIGN_IN_TOP_LEFT, GPS_STATUS_DEBUG_X, GPS_STATUS_DEBUG_Y);
    lv_label_set_text(gps_status_debug_text, "");
    #else
    for ( uint8_t i = 0; i < GPS_STATUS_DEBUG_ROW_COUNT; i++ ) {
        gps_status_debug_rows[i] = lv_label_create(gps_status_main_tile, NULL);
        lv_obj_add_style(gps_status_debug_rows[i], LV_OBJ_PART_MAIN, &gps_status_value_style);
        lv_obj_set_width(gps_status_debug_rows[i], lv_disp_get_hor_res(NULL) - ( GPS_STATUS_DEBUG_X * 2 ) );
        lv_label_set_long_mode(gps_status_debug_rows[i], LV_LABEL_LONG_CROP);
        lv_label_set_align(gps_status_debug_rows[i], LV_LABEL_ALIGN_LEFT);
        lv_label_set_text(gps_status_debug_rows[i], "");
        lv_obj_align(gps_status_debug_rows[i], gps_status_main_tile, LV_ALIGN_IN_TOP_LEFT, GPS_STATUS_DEBUG_X, GPS_STATUS_DEBUG_Y + ( i * GPS_STATUS_DEBUG_STEP ) );
    }
    #endif

    gpsctl_register_cb(     GPSCTL_FIX
                          | GPSCTL_NOFIX
                          | GPSCTL_UPDATE_LOCATION
                          | GPSCTL_UPDATE_SATELLITE
                          | GPSCTL_UPDATE_SATELLITE_TYPE
                          | GPSCTL_UPDATE_SPEED
                          | GPSCTL_UPDATE_ALTITUDE
                          | GPSCTL_UPDATE_SOURCE
                          , gpsctl_gps_status_event_cb
                          , "gpsctl gps status" );
    gps_status_block_return_maintile = display_get_block_return_maintile();
    mainbar_add_tile_activate_cb( tile_num, gps_status_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, gps_status_hibernate_cb );
    styles_register_cb( STYLE_CHANGE, style_change_event_cb, "gps status style");
    gps_status_debug_task = lv_task_create( gps_status_debug_task_cb, 1000, LV_TASK_PRIO_LOW, NULL );
    gps_status_update_debug_label();
    return;
#endif
    /*
     * num satfix
     */
    lv_obj_t *satfix_cont = lv_obj_create(gps_status_main_tile, NULL);
    lv_obj_set_size(satfix_cont, lv_disp_get_hor_res(NULL), STATUS_HEIGHT);
    lv_obj_add_style(satfix_cont, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_obj_align(satfix_cont, gps_status_main_tile, LV_ALIGN_IN_TOP_MID, 0, STATUSBAR_HEIGHT );
    lv_obj_t *satfix_label = lv_label_create(satfix_cont, NULL);
    lv_obj_add_style(satfix_label, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_text(satfix_label, "Fix");
    lv_obj_align(satfix_label, satfix_cont, LV_ALIGN_IN_LEFT_MID, 5, 0);
    satfix_value_on = lv_led_create(satfix_cont, NULL);
    lv_obj_add_style(satfix_value_on, LV_LED_PART_MAIN, &style_led_green);
    lv_obj_set_size(satfix_value_on, 15, 15);
    lv_obj_align(satfix_value_on, satfix_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_led_on(satfix_value_on);
    lv_obj_set_hidden(satfix_value_on, true);
    satfix_value_off = lv_led_create(satfix_cont, NULL);
    lv_obj_add_style(satfix_value_off, LV_LED_PART_MAIN, &style_led_red);
    lv_obj_set_size(satfix_value_off, 15, 15);
    lv_obj_align(satfix_value_off, satfix_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_led_on(satfix_value_off);
    lv_obj_set_hidden(satfix_value_off, false);
    /*
     * num satellites
     */
    lv_obj_t *num_satellites_cont = lv_obj_create(gps_status_main_tile, NULL);
    lv_obj_set_size(num_satellites_cont, lv_disp_get_hor_res(NULL), STATUS_HEIGHT);
    lv_obj_add_style(num_satellites_cont, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_obj_align(num_satellites_cont, satfix_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_t *num_satellites_label = lv_label_create(num_satellites_cont, NULL);
    lv_obj_add_style(num_satellites_label, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_text(num_satellites_label, "Power");
    lv_obj_align(num_satellites_label, num_satellites_cont, LV_ALIGN_IN_LEFT_MID, 5, 0);
    num_satellites_value = lv_label_create(num_satellites_cont, NULL);
    lv_obj_add_style(num_satellites_value, LV_OBJ_PART_MAIN, &gps_status_value_style);
    gps_status_config_value_label( num_satellites_value );
    lv_label_set_text(num_satellites_value, "n/a");
    lv_obj_align(num_satellites_value, num_satellites_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);
    /*
     * satellite type
     */
    lv_obj_t *satellite_type_cont = lv_obj_create(gps_status_main_tile, NULL);
    lv_obj_set_size(satellite_type_cont, lv_disp_get_hor_res(NULL), STATUS_HEIGHT);
    lv_obj_add_style(satellite_type_cont, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_obj_align(satellite_type_cont, num_satellites_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_t *satellite_type_label = lv_label_create(satellite_type_cont, NULL);
    lv_obj_add_style(satellite_type_label, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_text(satellite_type_label, "Probe");
    lv_obj_align(satellite_type_label, satellite_type_cont, LV_ALIGN_IN_LEFT_MID, 5, 0);
    satellite_type = lv_label_create(satellite_type_cont, NULL);
    lv_obj_add_style(satellite_type, LV_OBJ_PART_MAIN, &gps_status_value_style);
    gps_status_config_value_label( satellite_type );
    lv_label_set_text(satellite_type, "n/a");
    lv_obj_align(satellite_type, satellite_type_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);
    /*
     * altitude
     */
    lv_obj_t *altitude_cont = lv_obj_create(gps_status_main_tile, NULL);
    lv_obj_set_size(altitude_cont, lv_disp_get_hor_res(NULL), STATUS_HEIGHT);
    lv_obj_add_style(altitude_cont, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_obj_align(altitude_cont, satellite_type_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_t *altitude_info_label = lv_label_create(altitude_cont, NULL);
    lv_obj_add_style(altitude_info_label, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_text(altitude_info_label, "Raw");
    lv_obj_align(altitude_info_label, altitude_cont, LV_ALIGN_IN_LEFT_MID, 5, 0);
    altitude_value = lv_label_create(altitude_cont, NULL);
    lv_obj_add_style(altitude_value, LV_OBJ_PART_MAIN, &gps_status_value_style);
    gps_status_config_value_label( altitude_value );
    lv_label_set_text(altitude_value, "n/a");
    lv_obj_align(altitude_value, altitude_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);
    /*
     * long lat
     */
    lv_obj_t *pos_longlat_cont = lv_obj_create(gps_status_main_tile, NULL);
    lv_obj_set_size(pos_longlat_cont, lv_disp_get_hor_res(NULL), STATUS_HEIGHT);
    lv_obj_add_style(pos_longlat_cont, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_obj_align(pos_longlat_cont, altitude_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_t *pos_longlat_label = lv_label_create(pos_longlat_cont, NULL);
    lv_obj_add_style(pos_longlat_label, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_text(pos_longlat_label, "Pos");
    lv_obj_align(pos_longlat_label, pos_longlat_cont, LV_ALIGN_IN_LEFT_MID, 5, 0);
    pos_longlat_value = lv_label_create(pos_longlat_cont, NULL);
    lv_obj_add_style(pos_longlat_value, LV_OBJ_PART_MAIN, &gps_status_value_style);
    gps_status_config_value_label( pos_longlat_value );
    lv_label_set_text(pos_longlat_value, "n/a");
    lv_obj_align(pos_longlat_value, pos_longlat_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);
    /*
     * speed
     */
    lv_obj_t *speed_cont = lv_obj_create(gps_status_main_tile, NULL);
    lv_obj_set_size(speed_cont, lv_disp_get_hor_res(NULL), STATUS_HEIGHT);
    lv_obj_add_style(speed_cont, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_obj_align(speed_cont, pos_longlat_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_t *speed_label = lv_label_create(speed_cont, NULL);
    lv_obj_add_style(speed_label, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_text(speed_label, "GPS UTC");
    lv_obj_align(speed_label, speed_cont, LV_ALIGN_IN_LEFT_MID, 5, 0);
    speed_value = lv_label_create(speed_cont, NULL);
    lv_obj_add_style(speed_value, LV_OBJ_PART_MAIN, &gps_status_value_style);
    gps_status_config_value_label( speed_value );
    lv_label_set_text(speed_value, "n/a");
    lv_obj_align(speed_value, speed_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);
    /*
     * source label
     */
    lv_obj_t *source_cont = lv_obj_create(gps_status_main_tile, NULL);
    lv_obj_set_size(source_cont, lv_disp_get_hor_res(NULL), STATUS_HEIGHT);
    lv_obj_add_style(source_cont, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_obj_align(source_cont, speed_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_t *source_label = lv_label_create(source_cont, NULL);
    lv_obj_add_style(source_label, LV_OBJ_PART_MAIN, &gps_status_value_style);
    lv_label_set_text(source_label, "Sync");
    lv_obj_align(source_label, source_cont, LV_ALIGN_IN_LEFT_MID, 5, 0);
    source_value = lv_label_create(source_cont, NULL);
    lv_obj_add_style(source_value, LV_OBJ_PART_MAIN, &gps_status_value_style);
    gps_status_config_value_label( source_value );
    lv_label_set_text(source_value, "n/a");
    lv_obj_align(source_value, source_cont, LV_ALIGN_IN_RIGHT_MID, -5, 0);

    for ( uint8_t i = 0; i < 5; i++ ) {
        gps_status_debug_rows[i] = lv_label_create(gps_status_main_tile, NULL);
        lv_obj_add_style(gps_status_debug_rows[i], LV_OBJ_PART_MAIN, &gps_status_value_style);
        lv_obj_set_width(gps_status_debug_rows[i], lv_disp_get_hor_res(NULL) - 20);
        lv_label_set_long_mode(gps_status_debug_rows[i], LV_LABEL_LONG_CROP);
        lv_label_set_align(gps_status_debug_rows[i], LV_LABEL_ALIGN_LEFT);
        lv_label_set_text(gps_status_debug_rows[i], "");
        if ( i == 0 ) {
            lv_obj_align(gps_status_debug_rows[i], source_cont, LV_ALIGN_OUT_BOTTOM_LEFT, 10, THEME_PADDING);
        }
        else {
            lv_obj_align(gps_status_debug_rows[i], gps_status_debug_rows[i - 1], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
        }
    }
    /*
     * create callback
     */
    gpsctl_register_cb(     GPSCTL_FIX 
                          | GPSCTL_NOFIX
                          | GPSCTL_UPDATE_LOCATION
                          | GPSCTL_UPDATE_SATELLITE
                          | GPSCTL_UPDATE_SATELLITE_TYPE
                          | GPSCTL_UPDATE_SPEED
                          | GPSCTL_UPDATE_ALTITUDE
                          | GPSCTL_UPDATE_SOURCE
                          , gpsctl_gps_status_event_cb
                          , "gpsctl gps status" );
    /** register avtivate and hibernate callback function */
    gps_status_block_return_maintile = display_get_block_return_maintile();
    mainbar_add_tile_activate_cb( tile_num, gps_status_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, gps_status_hibernate_cb );
    styles_register_cb( STYLE_CHANGE, style_change_event_cb, "gps status style");
    gps_status_debug_task = lv_task_create( gps_status_debug_task_cb, 1000, LV_TASK_PRIO_LOW, NULL );
    gps_status_update_debug_label();
}

bool style_change_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case STYLE_CHANGE:  lv_style_copy(&gps_status_value_style, ws_get_mainbar_style());
                            lv_style_set_bg_color(&gps_status_value_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK);
                            lv_style_set_bg_opa(&gps_status_value_style, LV_OBJ_PART_MAIN, LV_OPA_0);
                            lv_style_set_border_width(&gps_status_value_style, LV_OBJ_PART_MAIN, 0);
                            lv_style_set_text_font(&gps_status_value_style, LV_STATE_DEFAULT, &Ubuntu_16px);
                            #if defined( LILYGO_WATCH_ULTRA ) && !defined( GPS_STATUS_FULL_DEBUG_LAYOUT )
                                lv_style_set_text_font(&gps_status_value_style, LV_STATE_DEFAULT, &lv_font_montserrat_22);
                            #endif
                            for ( uint8_t i = 0; i < 12; i++ ) {
                                if ( gps_status_debug_rows[i] ) {
                                    lv_obj_add_style(gps_status_debug_rows[i], LV_OBJ_PART_MAIN, &gps_status_value_style);
                                }
                            }
                            if ( gps_status_debug_text ) {
                                lv_obj_add_style(gps_status_debug_text, LV_OBJ_PART_MAIN, &gps_status_value_style);
                            }
                            gps_status_update_debug_label();
                            break;
    }
    return( true );
}

static void gps_status_config_value_label( lv_obj_t *label ) {
    if ( !label ) {
        return;
    }

    lv_obj_set_width( label, lv_disp_get_hor_res( NULL ) - 88 );
    lv_label_set_long_mode( label, LV_LABEL_LONG_CROP );
    lv_label_set_align( label, LV_LABEL_ALIGN_RIGHT );
}

static void gps_status_format_age( uint32_t age_ms, char *buf, size_t len ) {
    if ( !buf || len == 0 ) {
        return;
    }

    if ( age_ms == UINT32_MAX ) {
        snprintf( buf, len, "never" );
    }
    else if ( age_ms < 1000 ) {
        snprintf( buf, len, "%lums", (unsigned long)age_ms );
    }
    else {
        snprintf( buf, len, "%lus", (unsigned long)( age_ms / 1000 ) );
    }
}

static void gps_status_update_debug_label( void ) {
    gpsctl_debug_t debug;
    char rx_age[12] = "";
    char sentence_age[12] = "";
    char sync_age[12] = "";
    char pos_buf[48] = "";
    char utc_buf[32] = "";
    char sync_buf[24] = "";

    gpsctl_get_debug( &debug );
    gps_status_format_age( debug.last_rx_age_ms, rx_age, sizeof( rx_age ) );
    gps_status_format_age( debug.last_sentence_age_ms, sentence_age, sizeof( sentence_age ) );
    gps_status_format_age( debug.last_time_sync_age_ms, sync_age, sizeof( sync_age ) );

    const char *probe = debug.probe_done ? ( debug.probe_ok ? "ok" : "fail" ) : "wait";
    uint32_t active_baud = debug.active_baud ? debug.active_baud : debug.baud;
    const char *last_sentence = debug.last_sentence[0] ? debug.last_sentence : "none";

    if ( debug.valid_location ) {
        snprintf( pos_buf, sizeof( pos_buf ), "%.5f, %.5f", debug.lat, debug.lon );
    }
    else {
        snprintf( pos_buf, sizeof( pos_buf ), "none" );
    }

    if ( debug.valid_date && debug.valid_time ) {
        snprintf(
            utc_buf,
            sizeof( utc_buf ),
            "%04u-%02u-%02u %02u:%02u:%02u",
            (unsigned)debug.year,
            (unsigned)debug.month,
            (unsigned)debug.day,
            (unsigned)debug.hour,
            (unsigned)debug.minute,
            (unsigned)debug.second
        );
    }
    else {
        snprintf( utc_buf, sizeof( utc_buf ), "none" );
    }

    if ( debug.time_sync_count > 0 ) {
        snprintf( sync_buf, sizeof( sync_buf ), "#%lu %s", (unsigned long)debug.time_sync_count, sync_age );
    }
    else {
        snprintf( sync_buf, sizeof( sync_buf ), "none" );
    }

    #if defined( GPS_STATUS_FULL_DEBUG_LAYOUT )
    #if defined( LILYGO_T_DECK_PLUS )
    if ( gps_status_debug_text ) {
        char debug_text[640] = "";
        snprintf(
            debug_text,
            sizeof( debug_text ),
            "Fix:%s  Power:%s  UART:%s\n"
            "Probe:%s %s  Baud:%lu\n"
            "Raw RX:%lu  last:%s\n"
            "NMEA ok:%lu bad:%lu  Ch:%lu\n"
            "Position:%s\n"
            "GPS UTC:%s\n"
            "Time sync:%s\n"
            "Pins: RX%ld TX%ld\n"
            "Last:%s age:%s\n"
            "Sats:%lu GP%lu GL%lu BD%lu",
            debug.valid_location ? "yes" : "no",
            debug.enabled ? "on" : "off",
            debug.serial_available ? "ok" : "none",
            probe,
            debug.probe_model,
            (unsigned long)active_baud,
            (unsigned long)debug.rx_bytes,
            rx_age,
            (unsigned long)debug.passed_checksum,
            (unsigned long)debug.failed_checksum,
            (unsigned long)debug.chars_processed,
            pos_buf,
            utc_buf,
            sync_buf,
            (long)debug.rx_pin,
            (long)debug.tx_pin,
            last_sentence,
            sentence_age,
            (unsigned long)debug.satellites,
            (unsigned long)debug.gps_satellites,
            (unsigned long)debug.glonass_satellites,
            (unsigned long)debug.baidou_satellites
        );
        lv_label_set_text(gps_status_debug_text, debug_text);
        lv_label_set_long_mode(gps_status_debug_text, LV_LABEL_LONG_BREAK);
        lv_obj_set_size(gps_status_debug_text, RES_X_MAX - ( GPS_STATUS_DEBUG_X * 2 ), RES_Y_MAX - GPS_STATUS_DEBUG_Y - 42);
        lv_obj_align(gps_status_debug_text, gps_status_main_tile, LV_ALIGN_IN_TOP_LEFT, GPS_STATUS_DEBUG_X, GPS_STATUS_DEBUG_Y);
        return;
    }
    #else
    if ( gps_status_debug_rows[GPS_STATUS_DEBUG_ROW_COUNT - 1] ) {
        lv_label_set_text_fmt( gps_status_debug_rows[0], "Fix:%s  Pwr:%s  UART:%s",
            debug.valid_location ? "yes" : "no",
            debug.enabled ? "on" : "off",
            debug.serial_available ? "ok" : "none"
        );
        lv_label_set_text_fmt( gps_status_debug_rows[1], "Probe:%s %s  %lu", probe, debug.probe_model, (unsigned long)active_baud );
        lv_label_set_text_fmt( gps_status_debug_rows[2], "RX:%lu %s  NMEA:%lu/%lu",
            (unsigned long)debug.rx_bytes,
            rx_age,
            (unsigned long)debug.passed_checksum,
            (unsigned long)debug.failed_checksum
        );
        lv_label_set_text_fmt( gps_status_debug_rows[3], "Position:%s", pos_buf );
        lv_label_set_text_fmt( gps_status_debug_rows[4], "GPS UTC:%s", utc_buf );
        lv_label_set_text_fmt( gps_status_debug_rows[5], "Time sync:%s", sync_buf );
        lv_label_set_text_fmt( gps_status_debug_rows[6], "Pins: RX%ld TX%ld", (long)debug.rx_pin, (long)debug.tx_pin );
        lv_label_set_text_fmt( gps_status_debug_rows[7], "Last:%s  %s", last_sentence, sentence_age );
        lv_label_set_text_fmt( gps_status_debug_rows[8], "Chars:%lu  fix:%lu",
            (unsigned long)debug.chars_processed,
            (unsigned long)debug.sentences_with_fix
        );
        lv_label_set_text_fmt( gps_status_debug_rows[9], "Sats:%lu GP%lu GL%lu BD%lu",
            (unsigned long)debug.satellites,
            (unsigned long)debug.gps_satellites,
            (unsigned long)debug.glonass_satellites,
            (unsigned long)debug.baidou_satellites
        );
        for ( uint8_t i = 0; i < GPS_STATUS_DEBUG_ROW_COUNT; i++ ) {
            lv_obj_align(gps_status_debug_rows[i], gps_status_main_tile, LV_ALIGN_IN_TOP_LEFT, GPS_STATUS_DEBUG_X, GPS_STATUS_DEBUG_Y + ( i * GPS_STATUS_DEBUG_STEP ) );
        }
        return;
    }
    #endif
    #endif

    if ( !gps_status_debug_rows[0] ) {
        return;
    }

    lv_obj_set_hidden( satfix_value_on, !debug.valid_location );
    lv_obj_set_hidden( satfix_value_off, debug.valid_location );
    lv_label_set_text_fmt( num_satellites_value, "%s uart:%s", debug.enabled ? "on" : "off", debug.serial_available ? "ok" : "none" );
    lv_label_set_text_fmt( satellite_type, "%s %s @%lu", probe, debug.probe_model, (unsigned long)active_baud );
    lv_label_set_text_fmt( altitude_value, "%lu last:%s", (unsigned long)debug.rx_bytes, rx_age );
    if ( debug.valid_location ) {
        char pos_buf[36] = "";
        snprintf( pos_buf, sizeof( pos_buf ), "%.5f %.5f", debug.lat, debug.lon );
        lv_label_set_text( pos_longlat_value, pos_buf );
    }
    else {
        lv_label_set_text_fmt( pos_longlat_value, "no pos sats:%lu", (unsigned long)debug.satellites );
    }
    if ( debug.valid_date && debug.valid_time ) {
        lv_label_set_text_fmt(
            speed_value,
            "%02u:%02u:%02u %02u/%02u",
            (unsigned)debug.hour,
            (unsigned)debug.minute,
            (unsigned)debug.second,
            (unsigned)debug.month,
            (unsigned)debug.day
        );
    }
    else {
        lv_label_set_text( speed_value, "no gps time" );
    }
    if ( debug.time_sync_count > 0 ) {
        lv_label_set_text_fmt( source_value, "#%lu %s", (unsigned long)debug.time_sync_count, sync_age );
    }
    else {
        lv_label_set_text( source_value, "none" );
    }

    lv_label_set_text_fmt( gps_status_debug_rows[0], "UART RX%ld TX%ld @%lu", (long)debug.rx_pin, (long)debug.tx_pin, (unsigned long)active_baud );
    lv_label_set_text_fmt( gps_status_debug_rows[1], "NMEA ok/bad:%lu/%lu", (unsigned long)debug.passed_checksum, (unsigned long)debug.failed_checksum );
    lv_label_set_text_fmt( gps_status_debug_rows[2], "Sentence:%s age:%s", last_sentence, sentence_age );
    lv_label_set_text_fmt( gps_status_debug_rows[3], "Chars:%lu fixSeen:%lu", (unsigned long)debug.chars_processed, (unsigned long)debug.sentences_with_fix );
    lv_label_set_text_fmt( gps_status_debug_rows[4], "Sats:%lu G%lu L%lu B%lu", (unsigned long)debug.satellites, (unsigned long)debug.gps_satellites, (unsigned long)debug.glonass_satellites, (unsigned long)debug.baidou_satellites );

    lv_obj_align( pos_longlat_value, lv_obj_get_parent( pos_longlat_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( num_satellites_value, lv_obj_get_parent( num_satellites_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( satellite_type, lv_obj_get_parent( satellite_type ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( speed_value, lv_obj_get_parent( speed_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( altitude_value, lv_obj_get_parent( altitude_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( source_value, lv_obj_get_parent( source_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
}

void gps_status_debug_task_cb(lv_task_t *task) {
    (void)task;
    if ( !gps_status_active ) {
        return;
    }
    gps_status_update_debug_label();
}

bool gpsctl_gps_status_event_cb( EventBits_t event, void *arg ) {
    char temp[30] = "";
    gps_data_t *gps_data = (gps_data_t*)arg;

    if ( !gps_status_active ) {
        return( true );
    }

    #if defined( GPS_STATUS_FULL_DEBUG_LAYOUT )
    if ( gps_status_debug_text || gps_status_debug_rows[GPS_STATUS_DEBUG_ROW_COUNT - 1] ) {
        gps_status_update_debug_label();
        return( true );
    }
    #endif

    switch( event ) {
        case GPSCTL_FIX:
            lv_obj_set_hidden( satfix_value_on, false );
            lv_obj_set_hidden( satfix_value_off, true );
            break;
        case GPSCTL_NOFIX:
            lv_obj_set_hidden( satfix_value_on, true );
            lv_obj_set_hidden( satfix_value_off, false );
            lv_label_set_text( pos_longlat_value, "n/a" );
            lv_label_set_text( num_satellites_value, "n/a" );
            lv_label_set_text( satellite_type, "n/a" );
            lv_label_set_text( altitude_value, "n/a" );
            lv_label_set_text( speed_value, "n/a" );
            lv_label_set_text( source_value, "n/a" );
            break;
        case GPSCTL_UPDATE_LOCATION:
            if( gps_data->valid_location )
                snprintf( temp, sizeof( temp ), "%.4f/%.4f", gps_data->lat, gps_data->lon );
            else
                snprintf( temp, sizeof( temp ), "n/a" );
            lv_label_set_text( pos_longlat_value, temp );
            break;
        case GPSCTL_UPDATE_SATELLITE:
            if ( gps_data->valid_satellite )
                snprintf( temp, sizeof( temp ), "%d", gps_data->satellites );
            else
                snprintf( temp, sizeof( temp ), "n/a" );
            lv_label_set_text( num_satellites_value, temp );
            break;
        case GPSCTL_UPDATE_SATELLITE_TYPE:
            snprintf( temp, sizeof( temp ), "GP %d, GL %d, BD %d", gps_data->satellite_types.gps_satellites,
                                                                   gps_data->satellite_types.glonass_satellites,
                                                                   gps_data->satellite_types.baidou_satellites );
            lv_label_set_text( satellite_type, temp );
            break;
        case GPSCTL_UPDATE_SPEED:
            if ( gps_data->valid_speed )
                snprintf( temp, sizeof( temp ), "%.2fkm/h", gps_data->speed_kmh );
            else
                snprintf( temp, sizeof( temp ), "n/a" );
            lv_label_set_text( speed_value, temp );
            break;
        case GPSCTL_UPDATE_ALTITUDE:
            if ( gps_data->valid_altitude )
                snprintf( temp, sizeof( temp ), "%.1fm", gps_data->altitude_meters );
            else
                snprintf( temp, sizeof( temp ), "n/a" );
            lv_label_set_text( altitude_value, temp);
            break;
        case GPSCTL_UPDATE_SOURCE:
            lv_label_set_text( source_value, gpsctl_get_source_str( gps_data->gps_source ) );
            break;
    }

    lv_obj_align( pos_longlat_value, lv_obj_get_parent( pos_longlat_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( num_satellites_value, lv_obj_get_parent( num_satellites_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( satellite_type, lv_obj_get_parent( satellite_type ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( speed_value, lv_obj_get_parent( speed_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( altitude_value, lv_obj_get_parent( altitude_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    lv_obj_align( source_value, lv_obj_get_parent( source_value ), LV_ALIGN_IN_RIGHT_MID, -5, 0);
    gps_status_update_debug_label();

    return( true );
}

void gps_status_hibernate_cb(void)
{
    gps_status_active = false;
    /** restore old "block the maintile value */
    display_set_block_return_maintile( gps_status_block_return_maintile );
    if ( gps_status_forced_gps ) {
        gpsctl_set_enable_on_standby( gps_status_prev_enable_on_standby );
        if ( !gps_status_prev_autoon ) {
            gpsctl_off();
        }
        gps_status_forced_gps = false;
    }
}
void gps_status_activate_cb(void)
{
    gps_status_active = true;
    /** save "block the maintile" value */
    gps_status_block_return_maintile = display_get_block_return_maintile();
    /** overwrite "block the maintile" value */
    display_set_block_return_maintile( true );
    gps_status_prev_autoon = gpsctl_get_autoon();
    gps_status_prev_enable_on_standby = gpsctl_get_enable_on_standby();
    gps_status_forced_gps = true;
    gpsctl_set_enable_on_standby( false );
    gpsctl_on();
    gps_status_update_debug_label();
}
