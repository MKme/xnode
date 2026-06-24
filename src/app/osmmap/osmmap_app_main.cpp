/****************************************************************************
 *   Aug 3 12:17:11 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
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

#include "osmmap_app.h"
#include "osmmap_app_main.h"
#include "config/osmmap_config.h"

#include "gui/gui.h"
#include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "hardware/display.h"
#include "hardware/gpsctl.h"
#include "hardware/blectl.h"
#include "hardware/wifictl.h"
#include "hardware/touch.h"
#include "hardware/powermgm.h"

#include "utils/osm_map/osm_map.h"
#include "utils/uri_load/uri_load.h"
#include "utils/json_psram_allocator.h"

#include <cmath>
#include <cstdio>

#ifdef NATIVE_64BIT
    #include <iostream>
    #include <fstream>
    #include <math.h>
    #include <string.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <pwd.h>
    #include "utils/logging.h"
    #include "utils/millis.h"
    #include <string>
    #include "utils/osm_map/osmtileserver.h"

    using namespace std;
    #define String string

    uint32_t eventmask = 0;
    const uint8_t * osm_server_json_start = osmtileserver_json;
#else
    #include <Arduino.h>
    #include <FS.h>
    #include <SPIFFS.h>
    #include <sys/stat.h>
    #include "gui/mainbar/setup_tile/watchface/watchface_tile.h"

    EventGroupHandle_t osmmap_event_handle = NULL;                  /** @brief osm tile image update event queue */
    TaskHandle_t _osmmap_update_Task;                               /** @brief osm tile image update Task */
    TaskHandle_t _osmmap_load_ahead_Task;                           /** @brief osm tile image update Task */

    extern const uint8_t osm_server_json_start[] asm("_binary_src_utils_osm_map_osmtileserver_json_start");
    extern const uint8_t osm_server_json_end[] asm("_binary_src_utils_osm_map_osmtileserver_json_end");
#endif

lv_task_t *osmmap_main_tile_task;                               /** @brief osm active/inactive task for show/hide user interface */

lv_obj_t *osmmap_app_main_tile = NULL;                          /** @brief osm main tile obj */
lv_obj_t *osmmap_app_tile_img = NULL;                           /** @brief osm tile image obj */
lv_obj_t *osmmap_app_pos_img = NULL;                            /** @brief osm position point obj */
lv_obj_t *osmmap_ext_pos_img = NULL;                            /** @brief external marker obj */
lv_obj_t *osmmap_lonlat_label = NULL;                           /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_gps_status_panel = NULL;                       /** @brief gps status panel obj */
lv_obj_t *osmmap_north_btn = NULL;                              /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_south_btn = NULL;                              /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_west_btn = NULL;                               /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_zoom_northwest_btn = NULL;                     /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_zoom_northeast_btn = NULL;                     /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_zoom_southwest_btn = NULL;                     /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_zoom_southeast_btn = NULL;                     /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_east_btn = NULL;                               /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_exit_btn = NULL;                               /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_zoom_in_btl = NULL;                            /** @brief osm zoom in icon/button obj */
lv_obj_t *osmmap_zoom_out_btl = NULL;                           /** @brief osm zoom out icon/button obj */

lv_obj_t *osmmap_layers_btn = NULL;                             /** @brief osm exit icon/button obj */
lv_obj_t *osmmap_top_menu = NULL;
lv_obj_t *osmmap_sub_menu_layers = NULL;
lv_obj_t *osmmap_sub_menu_setting = NULL;                       /** @brief osm style list box */

lv_style_t osmmap_app_main_style;                               /** @brief osm main styte obj */
lv_style_t osmmap_app_btn_style;                                /** @brief osm main styte obj */
lv_style_t osmmap_app_label_style;                              /** @brief osm main styte obj */
lv_style_t osmmap_app_nav_style;                                /** @brief osm main styte obj */

static volatile bool osmmap_app_active = false;                 /** @brief osm app active/inactive flag, true means active */
static volatile bool osmmap_block_return_maintile = false;      /** @brief osm block to maintile state store */
static volatile bool osmmap_block_show_messages = false;        /** @brief osm show messages state store */
static volatile bool osmmap_block_watchface = false;            /** @brief osm statusbar force dark mode state store */
static volatile bool osmmap_gps_state = false;                  /** @brief osm gps state on enter osmmap */
static volatile bool osmmap_gps_on_standby_state = false;       /** @brief osm gps on standby on enter osmmap */
static volatile bool osmmap_wifi_state = false;                 /** @brief osm wifi state on enter osmmap */
static volatile uint64_t last_touch = 0;
static bool osmmap_touch_pan_active = false;
static bool osmmap_touch_pan_consuming = false;
static int16_t osmmap_touch_pan_start_x = 0;
static int16_t osmmap_touch_pan_start_y = 0;
static int16_t osmmap_touch_pan_last_x = 0;
static int16_t osmmap_touch_pan_last_y = 0;
osm_location_t *osmmap_location = NULL;             /** @brief osm location obj */
osmmap_config_t osmmap_config;
static bool osmmap_external_marker_valid = false;
static double osmmap_external_marker_lon = 0.0;
static double osmmap_external_marker_lat = 0.0;
static char osmmap_external_marker_label[ 32 ] = { 0 };
static lv_obj_t *osmmap_overlay_layer = NULL;
static bool osmmap_have_local_position = false;
static double osmmap_local_lon = 0.0;
static double osmmap_local_lat = 0.0;
static bool osmmap_have_local_heading = false;
static double osmmap_local_heading = 0.0;
static bool osmmap_gps_enabled_seen = false;
static bool osmmap_gps_fix_seen = false;
static uint32_t osmmap_gps_satellites = 0;
static bool osmmap_local_marker_visible = false;
static uint32_t osmmap_last_gps_marker_refresh_ms = 0;
static char osmmap_last_gps_status_text[ 80 ] = { 0 };
static bool osmmap_watch_flash_mode = false;
static uint32_t osmmap_watch_flash_base_zoom = 10;
static uint32_t osmmap_watch_flash_projection_zoom = 9;
static uint32_t osmmap_watch_flash_render_zoom = 10;
static int32_t osmmap_watch_flash_pan_x = 0;
static int32_t osmmap_watch_flash_pan_y = 0;
static double osmmap_watch_flash_center_lon = 0.0;
static double osmmap_watch_flash_center_lat = 0.0;
static char osmmap_watch_flash_uri[ 160 ] = { 0 };
#if defined( LILYGO_WATCH_ULTRA )
static constexpr const char *OSMMAP_WATCH_MAP_NAME = "offline from watch sd";
static constexpr const char *OSMMAP_WATCH_TILE_ROOT = "/sd/osmmap";
static constexpr const char *OSMMAP_WATCH_CURRENT_TILE_PATH = "/sd/osmmap/current.png";
static constexpr const char *OSMMAP_WATCH_SEED_TILE_PATH = "/sd/osmmap/10/279/373.png";
static constexpr const char *OSMMAP_OVERLAY_CACHE_PATH = "/sd/osmmap/overlays.jsonl";
#else
static constexpr const char *OSMMAP_WATCH_MAP_NAME = "offline from watch flash";
static constexpr const char *OSMMAP_WATCH_TILE_ROOT = "/spiffs/osmmap";
static constexpr const char *OSMMAP_WATCH_CURRENT_TILE_PATH = "/spiffs/osmmap/current.png";
static constexpr const char *OSMMAP_WATCH_SEED_TILE_PATH = "/spiffs/osmmap/10/279/373.png";
static constexpr const char *OSMMAP_OVERLAY_CACHE_PATH = "/spiffs/osmmap/overlays.jsonl";
#endif
static uri_load_dsc_t *osmmap_watch_flash_image_load_dsc = NULL;
static lv_img_dsc_t osmmap_watch_flash_image_dsc = { 0 };
static bool osmmap_watch_flash_image_ready = false;
static bool osmmap_watch_flash_image_dirty = true;
static char osmmap_watch_flash_image_uri[ 160 ] = { 0 };

static const size_t OSMMAP_OVERLAY_MAX_ITEMS = 96;
static const size_t OSMMAP_OVERLAY_KEY_LEN = 48;
static const size_t OSMMAP_OVERLAY_LABEL_LEN = 32;
#if defined( LILYGO_WATCH_ULTRA )
static const lv_coord_t OSMMAP_LOCAL_MARKER_SIZE = 64;
static uint8_t osmmap_local_marker_image_buf[ LV_IMG_BUF_SIZE_TRUE_COLOR_ALPHA( OSMMAP_LOCAL_MARKER_SIZE, OSMMAP_LOCAL_MARKER_SIZE ) ] = { 0 };
static lv_img_dsc_t osmmap_local_marker_image;
static bool osmmap_local_marker_image_ready = false;
#endif

typedef enum {
    OSMMAP_OVERLAY_KIND_TEAM = 0,
    OSMMAP_OVERLAY_KIND_MESH,
    OSMMAP_OVERLAY_KIND_SITREP,
    OSMMAP_OVERLAY_KIND_CONTACT,
    OSMMAP_OVERLAY_KIND_TASK,
    OSMMAP_OVERLAY_KIND_CHECKIN,
    OSMMAP_OVERLAY_KIND_RESOURCE,
    OSMMAP_OVERLAY_KIND_ASSET,
    OSMMAP_OVERLAY_KIND_ZONE,
    OSMMAP_OVERLAY_KIND_MISSION,
    OSMMAP_OVERLAY_KIND_EVENT,
    OSMMAP_OVERLAY_KIND_PHASELINE,
    OSMMAP_OVERLAY_KIND_SENTINEL,
    OSMMAP_OVERLAY_KIND_ROUTE,
    OSMMAP_OVERLAY_KIND_COUNT,
    OSMMAP_OVERLAY_KIND_UNKNOWN = -1
} osmmap_overlay_kind_t;

typedef struct {
    bool used;
    osmmap_overlay_kind_t kind;
    double lon;
    double lat;
    uint32_t updated_at;
    bool has_pixel;
    int16_t pixel_x;
    int16_t pixel_y;
    bool has_color;
    uint8_t color_r;
    uint8_t color_g;
    uint8_t color_b;
    uint32_t replace_generation;
    char key[ OSMMAP_OVERLAY_KEY_LEN ];
    char label[ OSMMAP_OVERLAY_LABEL_LEN ];
    lv_obj_t *marker_obj;
    lv_obj_t *marker_label_obj;
} osmmap_overlay_item_t;

static osmmap_overlay_item_t osmmap_overlay_items[ OSMMAP_OVERLAY_MAX_ITEMS ] = { 0 };
static bool osmmap_overlay_replace_active = false;
static uint32_t osmmap_overlay_replace_generation = 0;
static bool osmmap_overlay_layer_enabled[ OSMMAP_OVERLAY_KIND_COUNT ] = {
    true,   /* team */
    true,   /* mesh */
    true,   /* sitrep */
    true,   /* contact */
    true,   /* task */
    true,   /* checkin */
    true,   /* resource */
    true,   /* asset */
    true,   /* zone */
    true,   /* mission */
    true,   /* event */
    true,   /* phaseline */
    true,   /* sentinel */
    true    /* route */
};

LV_IMG_DECLARE(layers_dark_48px);
LV_IMG_DECLARE(exit_dark_48px);
LV_IMG_DECLARE(zoom_in_dark_48px);
LV_IMG_DECLARE(zoom_out_dark_48px);
LV_IMG_DECLARE(osm_64px);
LV_IMG_DECLARE(info_fail_16px);
LV_IMG_DECLARE(info_ok_16px);
LV_IMG_DECLARE(checked_dark_16px);
LV_IMG_DECLARE(unchecked_dark_16px);
LV_FONT_DECLARE(Ubuntu_12px);
LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_32px);

void osmmap_main_tile_update_task( lv_task_t * task );
void osmmap_update_request( void );
void osmmap_update_Task( void * pvParameters );
void osmmap_load_ahead_Task( void * pvParameters );
static void osmmap_app_get_setting_menu_cb( lv_obj_t * obj, lv_event_t event );
void osmmap_app_set_setting_menu( lv_obj_t *menu );
bool osmmap_app_touch_event_cb( EventBits_t event, void *arg );
void osmmap_app_set_left_right_hand( bool left_right_hand );
static void nav_direction_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void nav_center_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void zoom_in_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void zoom_out_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void exit_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void osmmap_tile_server_event_cb( lv_obj_t * obj, lv_event_t event );
static void layers_btn_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
void osmmap_update_map( osm_location_t *osmmap_location, double lon, double lat, uint32_t zoom );
bool osmmap_gpsctl_event_cb( EventBits_t event, void *arg );
void osmmap_add_tile_server_list( lv_obj_t *layers_list );
void osmmap_activate_cb( void );
void osmmap_hibernate_cb( void );
bool osmmap_button_cb( EventBits_t event, void *arg );
static bool osmmap_is_watch_flash_source_name( const char *name );
static uint32_t osmmap_long2tilex( double lon, uint32_t z );
static uint32_t osmmap_lat2tiley( double lat, uint32_t z );
static double osmmap_lon2global_pixel_x( double lon, uint32_t z );
static double osmmap_lat2global_pixel_y( double lat, uint32_t z );
static bool osmmap_configure_watch_flash_source( double lon, double lat, uint32_t zoom, bool persist, uint32_t projection_zoom = 0xffffffffUL );
static void osmmap_reset_active_tile_image( void );
static void osmmap_release_watch_flash_image( bool clear_active_src );
static bool osmmap_load_watch_flash_image( bool force_reload );
static uint16_t osmmap_get_viewport_width( void );
static uint16_t osmmap_get_viewport_height( void );
static uint32_t osmmap_get_watch_flash_min_render_zoom( void );
static void osmmap_update_gps_status_label( void );
static void osmmap_update_watch_flash_status_label( void );
static void osmmap_clamp_watch_flash_pan( void );
static uint16_t osmmap_get_watch_flash_lvgl_zoom( void );
static uint16_t osmmap_get_display_lvgl_zoom( void );
static void osmmap_apply_image_zoom( void );
static void osmmap_configure_primary_control( lv_obj_t *button );
static void osmmap_raise_primary_controls( void );
static bool osmmap_accept_primary_control_event( lv_event_t event, uint32_t &last_event_ms );
static bool osmmap_obj_contains_touch_point( lv_obj_t *obj, int16_t x, int16_t y );
static bool osmmap_touch_starts_on_foreground_control( int16_t x, int16_t y );
static void osmmap_reset_touch_pan( void );
static bool osmmap_handle_touch_pan( const touch_t *touch );
static bool osmmap_watch_flash_uses_current_tile( void );
static bool osmmap_marker_uses_image_transform( void );
static bool osmmap_watch_flash_pixel_to_view( double pixel_x, double pixel_y, uint16_t *x, uint16_t *y );
static bool osmmap_project_watch_flash_current_lon_lat( double lon, double lat, uint16_t *x, uint16_t *y );
static bool osmmap_project_marker_lon_lat( double lon, double lat, uint16_t *x, uint16_t *y );
static void osmmap_place_marker( lv_obj_t *marker_obj, uint16_t marker_x, uint16_t marker_y );
static void osmmap_refresh_local_position_marker( void );
static void osmmap_refresh_marker_positions( void );
static bool osmmap_adjust_watch_flash_zoom( int delta );
static bool osmmap_adjust_watch_flash_pan( int32_t delta_x, int32_t delta_y );
static const char *osmmap_overlay_menu_label( osmmap_overlay_kind_t kind );
static osmmap_overlay_kind_t osmmap_overlay_kind_from_name( const char *name );
static const char *osmmap_overlay_kind_name( osmmap_overlay_kind_t kind );
static osmmap_overlay_kind_t osmmap_overlay_kind_from_menu_label( const char *label );
static const char *osmmap_overlay_badge_text( osmmap_overlay_kind_t kind );
static lv_color_t osmmap_overlay_bg_color( osmmap_overlay_kind_t kind );
static lv_color_t osmmap_overlay_text_color( osmmap_overlay_kind_t kind );
static lv_color_t osmmap_overlay_border_color( osmmap_overlay_kind_t kind );
static bool osmmap_overlay_parse_color( const char *value, uint8_t *r, uint8_t *g, uint8_t *b );
static lv_color_t osmmap_overlay_item_bg_color( const osmmap_overlay_item_t *item );
static lv_color_t osmmap_overlay_item_text_color( const osmmap_overlay_item_t *item );
static lv_color_t osmmap_overlay_item_border_color( const osmmap_overlay_item_t *item );
static bool osmmap_overlay_kind_uses_square_marker( osmmap_overlay_kind_t kind );
static uint16_t osmmap_overlay_marker_border_width( void );
static const lv_font_t *osmmap_overlay_marker_font( void );
static uint16_t osmmap_overlay_marker_size( osmmap_overlay_kind_t kind );
static uint16_t osmmap_overlay_marker_radius( osmmap_overlay_kind_t kind, uint16_t marker_size );
static lv_obj_t *osmmap_ensure_overlay_marker( osmmap_overlay_item_t *item );
static void osmmap_hide_overlay_marker( osmmap_overlay_item_t *item );
static void osmmap_reset_overlay_item( osmmap_overlay_item_t *item );
static double osmmap_normalize_heading( double heading );
static bool osmmap_point_inside_triangle( int32_t x, int32_t y, const lv_point_t *a, const lv_point_t *b, const lv_point_t *c );
static void osmmap_set_local_marker_pixel( lv_coord_t x, lv_coord_t y, lv_color_t color, lv_opa_t opa );
static void osmmap_prepare_local_marker_image( void );
static void osmmap_update_local_position_marker_heading( void );
static void osmmap_set_local_position_from_gps( const gps_data_t *gps_data );
static void osmmap_set_local_heading_from_gps( const gps_data_t *gps_data );
static void osmmap_raise_local_position_marker( void );
static void osmmap_create_local_position_marker( lv_obj_t *parent );

static const char *osmmap_overlay_menu_label( osmmap_overlay_kind_t kind ) {
    switch ( kind ) {
        case OSMMAP_OVERLAY_KIND_TEAM:      return( "Team members" );
        case OSMMAP_OVERLAY_KIND_MESH:      return( "Mesh nodes" );
        case OSMMAP_OVERLAY_KIND_SITREP:    return( "SITREPs" );
        case OSMMAP_OVERLAY_KIND_CONTACT:   return( "CONTACTs" );
        case OSMMAP_OVERLAY_KIND_TASK:      return( "TASKs" );
        case OSMMAP_OVERLAY_KIND_CHECKIN:   return( "CHECKINs" );
        case OSMMAP_OVERLAY_KIND_RESOURCE:  return( "Resource requests" );
        case OSMMAP_OVERLAY_KIND_ASSET:     return( "Assets" );
        case OSMMAP_OVERLAY_KIND_ZONE:      return( "Zones" );
        case OSMMAP_OVERLAY_KIND_MISSION:   return( "Missions" );
        case OSMMAP_OVERLAY_KIND_EVENT:     return( "Events" );
        case OSMMAP_OVERLAY_KIND_PHASELINE: return( "Phase lines" );
        case OSMMAP_OVERLAY_KIND_SENTINEL:  return( "Sentinel" );
        case OSMMAP_OVERLAY_KIND_ROUTE:     return( "Routes" );
        default:                            return( "" );
    }
}

static osmmap_overlay_kind_t osmmap_overlay_kind_from_name( const char *name ) {
    if ( !name ) {
        return( OSMMAP_OVERLAY_KIND_UNKNOWN );
    }
    if ( !strcmp( name, "team" ) ) return( OSMMAP_OVERLAY_KIND_TEAM );
    if ( !strcmp( name, "mesh" ) ) return( OSMMAP_OVERLAY_KIND_MESH );
    if ( !strcmp( name, "sitrep" ) ) return( OSMMAP_OVERLAY_KIND_SITREP );
    if ( !strcmp( name, "contact" ) ) return( OSMMAP_OVERLAY_KIND_CONTACT );
    if ( !strcmp( name, "task" ) ) return( OSMMAP_OVERLAY_KIND_TASK );
    if ( !strcmp( name, "checkin" ) ) return( OSMMAP_OVERLAY_KIND_CHECKIN );
    if ( !strcmp( name, "resource" ) ) return( OSMMAP_OVERLAY_KIND_RESOURCE );
    if ( !strcmp( name, "asset" ) ) return( OSMMAP_OVERLAY_KIND_ASSET );
    if ( !strcmp( name, "zone" ) ) return( OSMMAP_OVERLAY_KIND_ZONE );
    if ( !strcmp( name, "mission" ) ) return( OSMMAP_OVERLAY_KIND_MISSION );
    if ( !strcmp( name, "event" ) ) return( OSMMAP_OVERLAY_KIND_EVENT );
    if ( !strcmp( name, "phaseline" ) ) return( OSMMAP_OVERLAY_KIND_PHASELINE );
    if ( !strcmp( name, "sentinel" ) ) return( OSMMAP_OVERLAY_KIND_SENTINEL );
    if ( !strcmp( name, "route" ) ) return( OSMMAP_OVERLAY_KIND_ROUTE );
    return( OSMMAP_OVERLAY_KIND_UNKNOWN );
}

static const char *osmmap_overlay_kind_name( osmmap_overlay_kind_t kind ) {
    switch ( kind ) {
        case OSMMAP_OVERLAY_KIND_TEAM:      return( "team" );
        case OSMMAP_OVERLAY_KIND_MESH:      return( "mesh" );
        case OSMMAP_OVERLAY_KIND_SITREP:    return( "sitrep" );
        case OSMMAP_OVERLAY_KIND_CONTACT:   return( "contact" );
        case OSMMAP_OVERLAY_KIND_TASK:      return( "task" );
        case OSMMAP_OVERLAY_KIND_CHECKIN:   return( "checkin" );
        case OSMMAP_OVERLAY_KIND_RESOURCE:  return( "resource" );
        case OSMMAP_OVERLAY_KIND_ASSET:     return( "asset" );
        case OSMMAP_OVERLAY_KIND_ZONE:      return( "zone" );
        case OSMMAP_OVERLAY_KIND_MISSION:   return( "mission" );
        case OSMMAP_OVERLAY_KIND_EVENT:     return( "event" );
        case OSMMAP_OVERLAY_KIND_PHASELINE: return( "phaseline" );
        case OSMMAP_OVERLAY_KIND_SENTINEL:  return( "sentinel" );
        case OSMMAP_OVERLAY_KIND_ROUTE:     return( "route" );
        default:                            return( "" );
    }
}

static osmmap_overlay_kind_t osmmap_overlay_kind_from_menu_label( const char *label ) {
    for ( int i = 0; i < (int)OSMMAP_OVERLAY_KIND_COUNT; i++ ) {
        if ( !strcmp( label ? label : "", osmmap_overlay_menu_label( (osmmap_overlay_kind_t)i ) ) ) {
            return( (osmmap_overlay_kind_t)i );
        }
    }
    return( OSMMAP_OVERLAY_KIND_UNKNOWN );
}

static const char *osmmap_overlay_badge_text( osmmap_overlay_kind_t kind ) {
    switch ( kind ) {
        case OSMMAP_OVERLAY_KIND_TEAM:      return( "U" );
        case OSMMAP_OVERLAY_KIND_MESH:      return( "M" );
        case OSMMAP_OVERLAY_KIND_SITREP:    return( "S" );
        case OSMMAP_OVERLAY_KIND_CONTACT:   return( "!" );
        case OSMMAP_OVERLAY_KIND_TASK:      return( "T" );
        case OSMMAP_OVERLAY_KIND_CHECKIN:   return( "C" );
        case OSMMAP_OVERLAY_KIND_RESOURCE:  return( "B" );
        case OSMMAP_OVERLAY_KIND_ASSET:     return( "A" );
        case OSMMAP_OVERLAY_KIND_ZONE:      return( "Z" );
        case OSMMAP_OVERLAY_KIND_MISSION:   return( "F" );
        case OSMMAP_OVERLAY_KIND_EVENT:     return( "E" );
        case OSMMAP_OVERLAY_KIND_PHASELINE: return( "L" );
        case OSMMAP_OVERLAY_KIND_SENTINEL:  return( "N" );
        case OSMMAP_OVERLAY_KIND_ROUTE:     return( "R" );
        default:                            return( "?" );
    }
}

static lv_color_t osmmap_overlay_bg_color( osmmap_overlay_kind_t kind ) {
    switch ( kind ) {
        case OSMMAP_OVERLAY_KIND_TEAM:      return( lv_color_make( 255, 255, 255 ) );
        case OSMMAP_OVERLAY_KIND_MESH:      return( lv_color_make( 0x5b, 0x7c, 0xfa ) );
        case OSMMAP_OVERLAY_KIND_SITREP:    return( lv_color_make( 0xf6, 0xc9, 0x45 ) );
        case OSMMAP_OVERLAY_KIND_CONTACT:   return( lv_color_make( 0xff, 0x6b, 0x6b ) );
        case OSMMAP_OVERLAY_KIND_TASK:      return( lv_color_make( 0x7f, 0xe2, 0x6c ) );
        case OSMMAP_OVERLAY_KIND_CHECKIN:   return( lv_color_make( 255, 255, 255 ) );
        case OSMMAP_OVERLAY_KIND_RESOURCE:  return( lv_color_make( 0xff, 0x9f, 0x7c ) );
        case OSMMAP_OVERLAY_KIND_ASSET:     return( lv_color_make( 0x7c, 0xe3, 0xff ) );
        case OSMMAP_OVERLAY_KIND_ZONE:      return( lv_color_make( 0xf6, 0xc9, 0x45 ) );
        case OSMMAP_OVERLAY_KIND_MISSION:   return( lv_color_make( 0xc5, 0x8b, 0xff ) );
        case OSMMAP_OVERLAY_KIND_EVENT:     return( lv_color_make( 0x6c, 0xf0, 0xd0 ) );
        case OSMMAP_OVERLAY_KIND_PHASELINE: return( lv_color_make( 0x7c, 0xc7, 0xff ) );
        case OSMMAP_OVERLAY_KIND_SENTINEL:  return( lv_color_make( 0xff, 0x5b, 0xd4 ) );
        case OSMMAP_OVERLAY_KIND_ROUTE:     return( lv_color_make( 0x7c, 0xc7, 0xff ) );
        default:                            return( lv_color_make( 0x7b, 0x87, 0x94 ) );
    }
}

static lv_color_t osmmap_overlay_text_color( osmmap_overlay_kind_t kind ) {
    switch ( kind ) {
        case OSMMAP_OVERLAY_KIND_MESH:
        case OSMMAP_OVERLAY_KIND_CONTACT:
        case OSMMAP_OVERLAY_KIND_MISSION:
        case OSMMAP_OVERLAY_KIND_SENTINEL:
            return( LV_COLOR_WHITE );
        default:
            return( LV_COLOR_BLACK );
    }
}

static lv_color_t osmmap_overlay_border_color( osmmap_overlay_kind_t kind ) {
    if ( kind == OSMMAP_OVERLAY_KIND_TEAM || kind == OSMMAP_OVERLAY_KIND_CHECKIN ) {
        return( lv_color_make( 40, 40, 40 ) );
    }
    return( lv_color_make( 18, 24, 38 ) );
}

static int osmmap_overlay_hex_nibble( char c ) {
    if ( c >= '0' && c <= '9' ) return( c - '0' );
    if ( c >= 'a' && c <= 'f' ) return( c - 'a' + 10 );
    if ( c >= 'A' && c <= 'F' ) return( c - 'A' + 10 );
    return( -1 );
}

static bool osmmap_overlay_parse_color( const char *value, uint8_t *r, uint8_t *g, uint8_t *b ) {
    const char *p = value;
    int n[ 6 ];

    if ( !p || !r || !g || !b ) {
        return( false );
    }
    if ( p[ 0 ] == '#' ) {
        p++;
    }
    if ( strlen( p ) != 6 ) {
        return( false );
    }

    for ( int i = 0; i < 6; i++ ) {
        n[ i ] = osmmap_overlay_hex_nibble( p[ i ] );
        if ( n[ i ] < 0 ) {
            return( false );
        }
    }

    *r = (uint8_t)( ( n[ 0 ] << 4 ) | n[ 1 ] );
    *g = (uint8_t)( ( n[ 2 ] << 4 ) | n[ 3 ] );
    *b = (uint8_t)( ( n[ 4 ] << 4 ) | n[ 5 ] );
    return( true );
}

static lv_color_t osmmap_overlay_item_bg_color( const osmmap_overlay_item_t *item ) {
    if ( item && item->has_color ) {
        return( lv_color_make( item->color_r, item->color_g, item->color_b ) );
    }
    return( osmmap_overlay_bg_color( item ? item->kind : OSMMAP_OVERLAY_KIND_UNKNOWN ) );
}

static bool osmmap_overlay_item_color_is_dark( const osmmap_overlay_item_t *item ) {
    uint16_t luminance = 255;

    if ( !item || !item->has_color ) {
        return( false );
    }
    luminance = (uint16_t)( ( (uint16_t)item->color_r * 299 + (uint16_t)item->color_g * 587 + (uint16_t)item->color_b * 114 ) / 1000 );
    return( luminance < 145 );
}

static lv_color_t osmmap_overlay_item_text_color( const osmmap_overlay_item_t *item ) {
    if ( item && item->has_color ) {
        return( osmmap_overlay_item_color_is_dark( item ) ? LV_COLOR_WHITE : LV_COLOR_BLACK );
    }
    return( osmmap_overlay_text_color( item ? item->kind : OSMMAP_OVERLAY_KIND_UNKNOWN ) );
}

static lv_color_t osmmap_overlay_item_border_color( const osmmap_overlay_item_t *item ) {
    if ( item && item->has_color ) {
        return( osmmap_overlay_item_color_is_dark( item ) ? LV_COLOR_WHITE : LV_COLOR_BLACK );
    }
    return( osmmap_overlay_border_color( item ? item->kind : OSMMAP_OVERLAY_KIND_UNKNOWN ) );
}

static bool osmmap_overlay_kind_uses_square_marker( osmmap_overlay_kind_t kind ) {
    switch ( kind ) {
        case OSMMAP_OVERLAY_KIND_ZONE:
        case OSMMAP_OVERLAY_KIND_PHASELINE:
        case OSMMAP_OVERLAY_KIND_ROUTE:
            return( true );
        default:
            return( false );
    }
}

static uint16_t osmmap_overlay_marker_border_width( void ) {
#if defined( LILYGO_WATCH_ULTRA )
    return( 4 );
#else
    return( 2 );
#endif
}

static const lv_font_t *osmmap_overlay_marker_font( void ) {
#if defined( LILYGO_WATCH_ULTRA )
    return( &Ubuntu_16px );
#else
    return( &Ubuntu_12px );
#endif
}

static uint16_t osmmap_overlay_marker_size( osmmap_overlay_kind_t kind ) {
    const uint16_t base_size = osmmap_overlay_kind_uses_square_marker( kind ) ? 14 : 16;

#if defined( LILYGO_WATCH_ULTRA )
    return( base_size * 2 );
#else
    return( base_size );
#endif
}

static uint16_t osmmap_overlay_marker_radius( osmmap_overlay_kind_t kind, uint16_t marker_size ) {
    if ( osmmap_overlay_kind_uses_square_marker( kind ) ) {
        return( (uint16_t)fmax( 3.0, (double)marker_size / 4.0 ) );
    }
    return( LV_RADIUS_CIRCLE );
}

static lv_obj_t *osmmap_ensure_overlay_marker( osmmap_overlay_item_t *item ) {
    lv_obj_t *parent = osmmap_overlay_layer ? osmmap_overlay_layer : ( osmmap_app_tile_img ? lv_obj_get_parent( osmmap_app_tile_img ) : NULL );
    uint16_t marker_size = item ? osmmap_overlay_marker_size( item->kind ) : 16;

    if ( !item || !parent ) {
        return( NULL );
    }
    if ( !item->marker_obj ) {
        item->marker_obj = lv_obj_create( parent, NULL );
        lv_obj_set_click( item->marker_obj, false );
        lv_obj_set_style_local_pad_all( item->marker_obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );
        lv_obj_set_style_local_bg_opa( item->marker_obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER );
        lv_obj_set_style_local_shadow_width( item->marker_obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );
        item->marker_label_obj = lv_label_create( item->marker_obj, NULL );
        lv_obj_set_style_local_bg_opa( item->marker_label_obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP );
        lv_obj_set_hidden( item->marker_obj, true );
    }
    lv_obj_set_click( item->marker_obj, false );
    lv_obj_set_size( item->marker_obj, marker_size, marker_size );
    lv_obj_set_style_local_border_width( item->marker_obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, osmmap_overlay_marker_border_width() );
    lv_obj_set_style_local_radius(
        item->marker_obj,
        LV_OBJ_PART_MAIN,
        LV_STATE_DEFAULT,
        osmmap_overlay_marker_radius( item->kind, marker_size )
    );
    lv_obj_set_style_local_bg_color( item->marker_obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, osmmap_overlay_item_bg_color( item ) );
    lv_obj_set_style_local_border_color( item->marker_obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, osmmap_overlay_item_border_color( item ) );
    if ( item->marker_label_obj ) {
        lv_obj_set_click( item->marker_label_obj, false );
        lv_label_set_text( item->marker_label_obj, osmmap_overlay_badge_text( item->kind ) );
        lv_obj_set_style_local_text_font( item->marker_label_obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, osmmap_overlay_marker_font() );
        lv_obj_set_style_local_text_color( item->marker_label_obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, osmmap_overlay_item_text_color( item ) );
        lv_obj_align( item->marker_label_obj, item->marker_obj, LV_ALIGN_CENTER, 0, 0 );
    }
    return( item->marker_obj );
}

static void osmmap_hide_overlay_marker( osmmap_overlay_item_t *item ) {
    if ( item && item->marker_obj ) {
        lv_obj_set_hidden( item->marker_obj, true );
    }
}

static void osmmap_reset_overlay_item( osmmap_overlay_item_t *item ) {
    if ( !item ) {
        return;
    }

    item->used = false;
    item->kind = OSMMAP_OVERLAY_KIND_UNKNOWN;
    item->lon = 0.0;
    item->lat = 0.0;
    item->updated_at = 0;
    item->has_pixel = false;
    item->pixel_x = 0;
    item->pixel_y = 0;
    item->has_color = false;
    item->color_r = 0;
    item->color_g = 0;
    item->color_b = 0;
    item->replace_generation = 0;
    item->key[ 0 ] = '\0';
    item->label[ 0 ] = '\0';
    osmmap_hide_overlay_marker( item );
}

static double osmmap_normalize_heading( double heading ) {
    if ( !std::isfinite( heading ) ) {
        return( 0.0 );
    }

    if ( fabs( heading ) > 360.0 ) {
        heading = heading / 100.0;
    }

    heading = fmod( heading, 360.0 );
    if ( heading < 0.0 ) {
        heading += 360.0;
    }
    return( heading );
}

static bool osmmap_point_inside_triangle( int32_t x, int32_t y, const lv_point_t *a, const lv_point_t *b, const lv_point_t *c ) {
    const int32_t edge_ab = ( x - a->x ) * ( b->y - a->y ) - ( y - a->y ) * ( b->x - a->x );
    const int32_t edge_bc = ( x - b->x ) * ( c->y - b->y ) - ( y - b->y ) * ( c->x - b->x );
    const int32_t edge_ca = ( x - c->x ) * ( a->y - c->y ) - ( y - c->y ) * ( a->x - c->x );

    return( ( edge_ab >= 0 && edge_bc >= 0 && edge_ca >= 0 ) ||
            ( edge_ab <= 0 && edge_bc <= 0 && edge_ca <= 0 ) );
}

static void osmmap_set_local_marker_pixel( lv_coord_t x, lv_coord_t y, lv_color_t color, lv_opa_t opa ) {
#if defined( LILYGO_WATCH_ULTRA )
    if ( x < 0 || y < 0 || x >= OSMMAP_LOCAL_MARKER_SIZE || y >= OSMMAP_LOCAL_MARKER_SIZE ) {
        return;
    }
    lv_img_buf_set_px_color( &osmmap_local_marker_image, x, y, color );
    lv_img_buf_set_px_alpha( &osmmap_local_marker_image, x, y, opa );
#endif
}

static void osmmap_prepare_local_marker_image( void ) {
#if defined( LILYGO_WATCH_ULTRA )
    if ( osmmap_local_marker_image_ready ) {
        return;
    }

    for ( size_t i = 0; i < sizeof( osmmap_local_marker_image_buf ); i++ ) {
        osmmap_local_marker_image_buf[ i ] = 0;
    }

    osmmap_local_marker_image.header.always_zero = 0;
    osmmap_local_marker_image.header.w = OSMMAP_LOCAL_MARKER_SIZE;
    osmmap_local_marker_image.header.h = OSMMAP_LOCAL_MARKER_SIZE;
    osmmap_local_marker_image.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    osmmap_local_marker_image.data_size = sizeof( osmmap_local_marker_image_buf );
    osmmap_local_marker_image.data = osmmap_local_marker_image_buf;

    const lv_point_t outer_tip = { OSMMAP_LOCAL_MARKER_SIZE / 2, 2 };
    const lv_point_t outer_right = { OSMMAP_LOCAL_MARKER_SIZE - 4, OSMMAP_LOCAL_MARKER_SIZE - 4 };
    const lv_point_t outer_left = { 4, OSMMAP_LOCAL_MARKER_SIZE - 4 };
    const lv_point_t inner_tip = { OSMMAP_LOCAL_MARKER_SIZE / 2, 8 };
    const lv_point_t inner_right = { OSMMAP_LOCAL_MARKER_SIZE - 11, OSMMAP_LOCAL_MARKER_SIZE - 10 };
    const lv_point_t inner_left = { 11, OSMMAP_LOCAL_MARKER_SIZE - 10 };

    for ( lv_coord_t y = 0; y < OSMMAP_LOCAL_MARKER_SIZE; y++ ) {
        for ( lv_coord_t x = 0; x < OSMMAP_LOCAL_MARKER_SIZE; x++ ) {
            if ( osmmap_point_inside_triangle( x, y, &outer_tip, &outer_right, &outer_left ) ) {
                osmmap_set_local_marker_pixel( x, y, LV_COLOR_BLACK, LV_OPA_80 );
            }
            if ( osmmap_point_inside_triangle( x, y, &inner_tip, &inner_right, &inner_left ) ) {
                osmmap_set_local_marker_pixel( x, y, LV_COLOR_WHITE, LV_OPA_COVER );
            }
        }
    }

    osmmap_local_marker_image_ready = true;
#endif
}

static void osmmap_update_local_position_marker_heading( void ) {
#if defined( LILYGO_WATCH_ULTRA )
    if ( !osmmap_app_pos_img ) {
        return;
    }

    const double heading = osmmap_have_local_heading ? osmmap_local_heading : 0.0;
    lv_img_set_angle( osmmap_app_pos_img, (int16_t)lround( heading * 10.0 ) );
#endif
}

static void osmmap_set_local_position_from_gps( const gps_data_t *gps_data ) {
    if ( !gps_data || !gps_data->valid_location ) {
        return;
    }

    osmmap_local_lon = gps_data->lon;
    osmmap_local_lat = gps_data->lat;
    osmmap_have_local_position = true;
}

static void osmmap_set_local_heading_from_gps( const gps_data_t *gps_data ) {
    if ( !gps_data || !gps_data->valid_course ) {
        return;
    }

    osmmap_local_heading = osmmap_normalize_heading( gps_data->course );
    osmmap_have_local_heading = true;
    osmmap_update_local_position_marker_heading();
}

static void osmmap_raise_local_position_marker( void ) {
    if ( osmmap_app_pos_img ) {
        lv_obj_move_foreground( osmmap_app_pos_img );
    }
}

static void osmmap_create_local_position_marker( lv_obj_t *parent ) {
#if defined( LILYGO_WATCH_ULTRA )
    osmmap_prepare_local_marker_image();

    osmmap_app_pos_img = lv_img_create( parent, NULL );
    lv_img_set_src( osmmap_app_pos_img, &osmmap_local_marker_image );
    lv_img_set_pivot( osmmap_app_pos_img, OSMMAP_LOCAL_MARKER_SIZE / 2, OSMMAP_LOCAL_MARKER_SIZE / 2 );
    lv_img_set_antialias( osmmap_app_pos_img, false );
    lv_obj_set_click( osmmap_app_pos_img, false );
    osmmap_update_local_position_marker_heading();
#else
    osmmap_app_pos_img = lv_img_create( parent, NULL );
    lv_img_set_src( osmmap_app_pos_img, &info_fail_16px );
    lv_obj_set_click( osmmap_app_pos_img, false );
#endif
}

static void osmmap_refresh_local_position_marker( void ) {
    if ( !osmmap_location || !osmmap_app_pos_img ) {
        osmmap_update_watch_flash_status_label();
        return;
    }

    if ( osmmap_have_local_position ) {
        uint16_t marker_x = 0;
        uint16_t marker_y = 0;
        bool local_marker_placed = false;

        if ( osmmap_project_marker_lon_lat( osmmap_local_lon, osmmap_local_lat, &marker_x, &marker_y ) ) {
            osmmap_place_marker( osmmap_app_pos_img, marker_x, marker_y );
            local_marker_placed = true;
        }
        else if ( osmmap_location->tilexy_pos_valid &&
                  fabs( osmmap_local_lon - osmmap_location->lon ) < 0.0000001 &&
                  fabs( osmmap_local_lat - osmmap_location->lat ) < 0.0000001 ) {
            osmmap_place_marker( osmmap_app_pos_img, osmmap_location->tilex_pos, osmmap_location->tiley_pos );
            local_marker_placed = true;
        }
        else if ( fabs( osmmap_local_lon - osmmap_location->lon ) < 0.0000001 &&
                  fabs( osmmap_local_lat - osmmap_location->lat ) < 0.0000001 ) {
            osmmap_place_marker( osmmap_app_pos_img, osmmap_get_viewport_width() / 2, osmmap_get_viewport_height() / 2 );
            local_marker_placed = true;
        }

        if ( !local_marker_placed ) {
            lv_obj_set_hidden( osmmap_app_pos_img, true );
        }
        osmmap_local_marker_visible = local_marker_placed;
    }
    else {
        lv_obj_set_hidden( osmmap_app_pos_img, true );
        osmmap_local_marker_visible = false;
    }

    osmmap_raise_local_position_marker();
    osmmap_update_gps_status_label();
}

static bool osmmap_is_watch_flash_source_name( const char *name ) {
    return( name && ( !strcmp( name, OSMMAP_WATCH_MAP_NAME ) ||
                      !strcmp( name, "offline from watch flash" ) ||
                      !strcmp( name, "offline from watch sd" ) ) );
}

static uint32_t osmmap_long2tilex( double lon, uint32_t z ) {
    return( (uint32_t)floor( ( lon + 180.0 ) / 360.0 * ( 1UL << z ) ) );
}

static uint32_t osmmap_lat2tiley( double lat, uint32_t z ) {
    const double latrad = lat * M_PI / 180.0;
    return( (uint32_t)floor( ( 1.0 - asinh( tan( latrad ) ) / M_PI ) / 2.0 * ( 1UL << z ) ) );
}

static double osmmap_lon2global_pixel_x( double lon, uint32_t z ) {
    return( ( ( lon + 180.0 ) / 360.0 ) * (double)( 1UL << z ) * 256.0 );
}

static double osmmap_lat2global_pixel_y( double lat, uint32_t z ) {
    const double clamped_lat = fmax( -85.05112878, fmin( 85.05112878, lat ) );
    const double latrad = clamped_lat * M_PI / 180.0;
    return( ( 1.0 - asinh( tan( latrad ) ) / M_PI ) * 0.5 * (double)( 1UL << z ) * 256.0 );
}

static bool osmmap_configure_watch_flash_source( double lon, double lat, uint32_t zoom, bool persist, uint32_t projection_zoom ) {
    const uint32_t clamped_zoom = zoom < 2 ? 2 : ( zoom > 18 ? 18 : zoom );
    uint32_t clamped_projection_zoom = projection_zoom == 0xffffffffUL ? ( clamped_zoom > 0 ? clamped_zoom - 1 : 0 ) : projection_zoom;
    const uint32_t tilex = osmmap_long2tilex( lon, clamped_zoom );
    const uint32_t tiley = osmmap_lat2tiley( lat, clamped_zoom );
    char tile_path_current[ 160 ] = { 0 };
    char tile_path_jpg[ 160 ] = { 0 };
    char tile_path_png[ 160 ] = { 0 };
    const char *tile_path = NULL;

    strlcpy( tile_path_current, OSMMAP_WATCH_CURRENT_TILE_PATH, sizeof( tile_path_current ) );
    snprintf( tile_path_jpg, sizeof( tile_path_jpg ), "%s/%u/%u/%u.jpg", OSMMAP_WATCH_TILE_ROOT, clamped_zoom, tilex, tiley );
    snprintf( tile_path_png, sizeof( tile_path_png ), "%s/%u/%u/%u.png", OSMMAP_WATCH_TILE_ROOT, clamped_zoom, tilex, tiley );
#ifndef NATIVE_64BIT
    struct stat st;

    if ( stat( tile_path_current, &st ) == 0 ) {
        tile_path = tile_path_current;
    }
    else if ( stat( tile_path_png, &st ) == 0 ) {
        tile_path = tile_path_png;
    }
    else if ( stat( tile_path_jpg, &st ) == 0 ) {
        tile_path = tile_path_jpg;
    }
    else {
        OSMMAP_APP_ERROR_LOG( "watch basemap tile missing: %s or %s", tile_path_jpg, tile_path_png );
        return( false );
    }
#else
    tile_path = tile_path_jpg;
#endif
    if ( clamped_projection_zoom > 22 ) {
        clamped_projection_zoom = clamped_zoom;
    }

    snprintf( osmmap_watch_flash_uri, sizeof( osmmap_watch_flash_uri ), "file://%s", tile_path );
    osmmap_watch_flash_mode = true;
    osmmap_watch_flash_base_zoom = clamped_zoom;
    osmmap_watch_flash_projection_zoom = clamped_projection_zoom;
    osmmap_watch_flash_render_zoom = clamped_zoom;
    osmmap_watch_flash_pan_x = 0;
    osmmap_watch_flash_pan_y = 0;
    osmmap_watch_flash_center_lon = lon;
    osmmap_watch_flash_center_lat = lat;
    osmmap_watch_flash_image_dirty = ( strcmp( tile_path, OSMMAP_WATCH_CURRENT_TILE_PATH ) == 0 );
    if ( !osmmap_watch_flash_image_dirty ) {
        osmmap_release_watch_flash_image( true );
    }

    if ( osmmap_location ) {
        if ( osmmap_app_active ) {
            osmmap_reset_active_tile_image();
        }
        osm_map_set_tile_server( osmmap_location, osmmap_watch_flash_uri );
        osm_map_clear_cache( osmmap_location );
        osm_map_set_zoom( osmmap_location, clamped_zoom );
        osm_map_set_lon_lat( osmmap_location, lon, lat );
        osm_map_center_location( osmmap_location );
    }

    if ( persist ) {
        osmmap_config.watch_flash_basemap_valid = true;
        osmmap_config.watch_flash_lon = lon;
        osmmap_config.watch_flash_lat = lat;
        osmmap_config.watch_flash_zoom = clamped_zoom;
        osmmap_config.save();
    }

    osmmap_apply_image_zoom();
    osmmap_update_watch_flash_status_label();
    if ( osmmap_watch_flash_image_dirty && osmmap_app_active ) {
        osmmap_load_watch_flash_image( true );
        osmmap_apply_image_zoom();
    }
    return( true );
}

static void osmmap_reset_active_tile_image( void ) {
    if ( !osmmap_app_active ) {
        return;
    }

    lv_img_cache_invalidate_src( NULL );
    if ( osmmap_app_tile_img ) {
        lv_img_set_src( osmmap_app_tile_img, osm_map_get_no_data_image() );
        lv_obj_invalidate( osmmap_app_tile_img );
    }
}

static void osmmap_release_watch_flash_image( bool clear_active_src ) {
    if ( clear_active_src && osmmap_app_active && osmmap_app_tile_img ) {
        lv_img_set_src( osmmap_app_tile_img, osm_map_get_no_data_image() );
        lv_obj_invalidate( osmmap_app_tile_img );
    }

    if ( osmmap_watch_flash_image_ready ) {
        lv_img_cache_invalidate_src( &osmmap_watch_flash_image_dsc );
    }
    if ( osmmap_watch_flash_image_load_dsc ) {
        uri_load_free_all( osmmap_watch_flash_image_load_dsc );
        osmmap_watch_flash_image_load_dsc = NULL;
    }

    osmmap_watch_flash_image_dsc.header.always_zero = 0;
    osmmap_watch_flash_image_dsc.header.cf = LV_IMG_CF_RAW_ALPHA;
    osmmap_watch_flash_image_dsc.header.w = 256;
    osmmap_watch_flash_image_dsc.header.h = 256;
    osmmap_watch_flash_image_dsc.data = NULL;
    osmmap_watch_flash_image_dsc.data_size = 0;
    osmmap_watch_flash_image_ready = false;
    osmmap_watch_flash_image_uri[ 0 ] = '\0';
}

static bool osmmap_load_watch_flash_image( bool force_reload ) {
    if ( !osmmap_watch_flash_uses_current_tile() || !osmmap_watch_flash_uri[ 0 ] ) {
        return( false );
    }

    if ( force_reload ||
         osmmap_watch_flash_image_dirty ||
         !osmmap_watch_flash_image_ready ||
         strcmp( osmmap_watch_flash_image_uri, osmmap_watch_flash_uri ) != 0 ) {
        uri_load_dsc_t *loaded = NULL;
        char uri[ sizeof( osmmap_watch_flash_uri ) ] = { 0 };

        strlcpy( uri, osmmap_watch_flash_uri, sizeof( uri ) );
        osmmap_release_watch_flash_image( true );

        loaded = uri_load_to_ram( uri );
        if ( !loaded || !loaded->data || loaded->size == 0 ) {
            if ( loaded ) {
                uri_load_free_all( loaded );
            }
            OSMMAP_APP_ERROR_LOG( "watch flash image load failed: %s", uri );
            osmmap_watch_flash_image_dirty = true;
            return( false );
        }

        osmmap_watch_flash_image_load_dsc = loaded;
        osmmap_watch_flash_image_dsc.header.always_zero = 0;
        osmmap_watch_flash_image_dsc.header.cf = LV_IMG_CF_RAW_ALPHA;
        osmmap_watch_flash_image_dsc.header.w = 256;
        osmmap_watch_flash_image_dsc.header.h = 256;
        osmmap_watch_flash_image_dsc.data = loaded->data;
        osmmap_watch_flash_image_dsc.data_size = loaded->size;
        strlcpy( osmmap_watch_flash_image_uri, uri, sizeof( osmmap_watch_flash_image_uri ) );
        osmmap_watch_flash_image_ready = true;
        osmmap_watch_flash_image_dirty = false;
        lv_img_cache_invalidate_src( &osmmap_watch_flash_image_dsc );
        OSMMAP_APP_INFO_LOG( "watch flash image loaded: %s bytes=%u", uri, (unsigned)loaded->size );
    }

    if ( osmmap_app_tile_img && osmmap_watch_flash_image_ready ) {
        lv_img_set_src( osmmap_app_tile_img, &osmmap_watch_flash_image_dsc );
        lv_obj_invalidate( osmmap_app_tile_img );
    }
    return( osmmap_watch_flash_image_ready );
}

static uint32_t osmmap_get_watch_flash_min_render_zoom( void ) {
    return( osmmap_watch_flash_base_zoom );
}

static uint16_t osmmap_get_viewport_width( void ) {
    const lv_coord_t width = lv_disp_get_hor_res( NULL );

#if defined( LILYGO_WATCH_ULTRA )
    return( width > 0 ? (uint16_t)width : 240 );
#elif defined( LILYGO_T_DECK_PLUS )
    return( width > 0 ? (uint16_t)width : 320 );
#else
    return( (uint16_t)( width > 512 ? width : 240 ) );
#endif
}

static uint16_t osmmap_get_viewport_height( void ) {
#if defined( LILYGO_WATCH_ULTRA )
    const lv_coord_t height = lv_disp_get_ver_res( NULL );

    return( height > 0 ? (uint16_t)height : 240 );
#elif defined( LILYGO_T_DECK_PLUS )
    const lv_coord_t height = lv_disp_get_ver_res( NULL );

    return( height > 0 ? (uint16_t)height : 240 );
#else
    const lv_coord_t width = lv_disp_get_hor_res( NULL );

    return( (uint16_t)( width > 512 ? width : 240 ) );
#endif
}

static double osmmap_get_view_cover_lvgl_zoom( void ) {
    double view_w = (double)osmmap_get_viewport_width();
    double view_h = (double)osmmap_get_viewport_height();

    if ( osmmap_app_tile_img ) {
        lv_obj_t *parent = lv_obj_get_parent( osmmap_app_tile_img );

        if ( parent && lv_obj_get_width( parent ) > 0 && lv_obj_get_height( parent ) > 0 ) {
            view_w = (double)lv_obj_get_width( parent );
            view_h = (double)lv_obj_get_height( parent );
        }
    }

    return( fmax( 256.0, fmax( view_w, view_h ) ) );
}

static double osmmap_get_tdeck_fit_height_lvgl_zoom( void ) {
#if defined( LILYGO_T_DECK_PLUS )
    const double view_w = (double)osmmap_get_viewport_width();
    const double view_h = (double)osmmap_get_viewport_height();
    const double fit_zoom = fmin( view_w, view_h );

    return( fit_zoom > 0.0 ? fit_zoom : 240.0 );
#else
    return( 256.0 );
#endif
}

static void osmmap_update_gps_status_label( void ) {
    if ( !osmmap_lonlat_label ) {
        return;
    }

    char status[ 80 ] = { 0 };

    if ( osmmap_have_local_position ) {
        if ( osmmap_local_marker_visible ) {
            snprintf( status, sizeof( status ), "GPS POS\n%.5f %.5f", osmmap_local_lat, osmmap_local_lon );
        }
        else {
            snprintf( status, sizeof( status ), "GPS OFFMAP\n%.5f %.5f", osmmap_local_lat, osmmap_local_lon );
        }
    }
    else if ( !osmmap_gps_enabled_seen ) {
        snprintf( status, sizeof( status ), "GPS OFF\nNO POS" );
    }
    else if ( osmmap_gps_fix_seen ) {
        snprintf( status, sizeof( status ), "GPS FIX\nNO POS" );
    }
    else if ( osmmap_gps_satellites > 0 ) {
        snprintf( status, sizeof( status ), "GPS NO POS\nSAT %lu", (unsigned long)osmmap_gps_satellites );
    }
    else {
        snprintf( status, sizeof( status ), "GPS NO POS\nNO FIX" );
    }

    if ( strcmp( osmmap_last_gps_status_text, status ) != 0 ) {
        strlcpy( osmmap_last_gps_status_text, status, sizeof( osmmap_last_gps_status_text ) );
        lv_label_set_text( osmmap_lonlat_label, status );
    }
#if defined( LILYGO_WATCH_ULTRA )
    if ( osmmap_gps_status_panel ) {
        lv_obj_set_hidden( osmmap_gps_status_panel, false );
        lv_obj_move_foreground( osmmap_gps_status_panel );
        lv_obj_set_width( osmmap_lonlat_label, lv_obj_get_width( osmmap_gps_status_panel ) - 10 );
        lv_obj_align( osmmap_lonlat_label, osmmap_gps_status_panel, LV_ALIGN_CENTER, 0, 0 );
    }
    lv_obj_set_hidden( osmmap_lonlat_label, false );
    osmmap_raise_local_position_marker();
#endif
}

static void osmmap_update_watch_flash_status_label( void ) {
    if ( !osmmap_lonlat_label || !osmmap_watch_flash_mode ) {
        return;
    }

    osmmap_update_gps_status_label();
}

static void osmmap_clamp_watch_flash_pan( void ) {
    if ( !osmmap_app_tile_img ) {
        osmmap_watch_flash_pan_x = 0;
        osmmap_watch_flash_pan_y = 0;
        return;
    }

    lv_obj_t *parent = lv_obj_get_parent( osmmap_app_tile_img );
    if ( !parent ) {
        osmmap_watch_flash_pan_x = 0;
        osmmap_watch_flash_pan_y = 0;
        return;
    }

    const double zoom_factor = (double)osmmap_get_watch_flash_lvgl_zoom() / 256.0;
    const int32_t view_w = lv_obj_get_width( parent );
    const int32_t view_h = lv_obj_get_height( parent );
    const int32_t image_w = 256;
    const int32_t image_h = 256;
    const int32_t scaled_w = (int32_t)lround( (double)image_w * zoom_factor );
    const int32_t scaled_h = (int32_t)lround( (double)image_h * zoom_factor );
    const int32_t max_pan_x = scaled_w > view_w ? ( scaled_w - view_w ) / 2 : 0;
    const int32_t max_pan_y = scaled_h > view_h ? ( scaled_h - view_h ) / 2 : 0;

    if ( osmmap_watch_flash_pan_x > max_pan_x ) osmmap_watch_flash_pan_x = max_pan_x;
    if ( osmmap_watch_flash_pan_x < -max_pan_x ) osmmap_watch_flash_pan_x = -max_pan_x;
    if ( osmmap_watch_flash_pan_y > max_pan_y ) osmmap_watch_flash_pan_y = max_pan_y;
    if ( osmmap_watch_flash_pan_y < -max_pan_y ) osmmap_watch_flash_pan_y = -max_pan_y;
}

static uint16_t osmmap_get_watch_flash_lvgl_zoom( void ) {
    const double zoom_factor = pow( 2.0, (double)osmmap_watch_flash_render_zoom - (double)osmmap_watch_flash_base_zoom );
    const double base_lvgl_zoom = osmmap_get_tdeck_fit_height_lvgl_zoom();
    const double lvgl_zoom = base_lvgl_zoom * zoom_factor;
    const double min_lvgl_zoom =
#if defined( LILYGO_WATCH_ULTRA )
        osmmap_get_view_cover_lvgl_zoom();
#else
        base_lvgl_zoom;
#endif
    const double clamped_zoom = fmax( min_lvgl_zoom, fmin( 2048.0, lvgl_zoom ) );
    return( (uint16_t)lround( clamped_zoom ) );
}

static uint16_t osmmap_get_display_lvgl_zoom( void ) {
    if ( osmmap_watch_flash_mode ) {
        return( osmmap_get_watch_flash_lvgl_zoom() );
    }
#if defined( M5PAPER )
    return( 540 );
#elif defined( LILYGO_WATCH_ULTRA )
    return( (uint16_t)lround( fmin( 2048.0, osmmap_get_view_cover_lvgl_zoom() ) ) );
#else
    return( 256 );
#endif
}

static void osmmap_apply_image_zoom( void ) {
    if ( !osmmap_app_tile_img ) {
        return;
    }

#ifdef M5PAPER
    if ( !osmmap_watch_flash_mode ) {
        lv_img_set_zoom( osmmap_app_tile_img, 540 );
        lv_obj_align( osmmap_app_tile_img, lv_obj_get_parent( osmmap_app_tile_img ), LV_ALIGN_CENTER, 0, 0 );
        osmmap_refresh_marker_positions();
        return;
    }
#endif
    lv_img_set_zoom( osmmap_app_tile_img, osmmap_get_display_lvgl_zoom() );
    if ( osmmap_watch_flash_mode ) {
        osmmap_clamp_watch_flash_pan();
        lv_obj_align( osmmap_app_tile_img, lv_obj_get_parent( osmmap_app_tile_img ), LV_ALIGN_CENTER, osmmap_watch_flash_pan_x, osmmap_watch_flash_pan_y );
        osmmap_refresh_marker_positions();
    }
    else {
        lv_obj_align( osmmap_app_tile_img, lv_obj_get_parent( osmmap_app_tile_img ), LV_ALIGN_CENTER, 0, 0 );
        osmmap_refresh_marker_positions();
    }
}

static void osmmap_configure_primary_control( lv_obj_t *button ) {
    if ( !button ) {
        return;
    }

#if defined( LILYGO_WATCH_ULTRA )
    const lv_coord_t target_size = 78;
    if ( lv_obj_get_width( button ) < target_size ) {
        lv_obj_set_width( button, target_size );
    }
    if ( lv_obj_get_height( button ) < target_size ) {
        lv_obj_set_height( button, target_size );
    }
    lv_obj_t *icon = lv_obj_get_child( button, NULL );
    if ( icon ) {
        lv_obj_align( icon, button, LV_ALIGN_CENTER, 0, 0 );
    }
    lv_obj_set_ext_click_area( button, 18, 18, 18, 18 );
#endif
    lv_obj_move_foreground( button );
}

static void osmmap_raise_primary_controls( void ) {
    osmmap_configure_primary_control( osmmap_layers_btn );
    osmmap_configure_primary_control( osmmap_exit_btn );
    osmmap_configure_primary_control( osmmap_zoom_in_btl );
    osmmap_configure_primary_control( osmmap_zoom_out_btl );
}

static bool osmmap_accept_primary_control_event( lv_event_t event, uint32_t &last_event_ms ) {
#if defined( LILYGO_WATCH_ULTRA )
    if ( event != LV_EVENT_PRESSED ) {
        return( false );
    }
#else
    if ( event != LV_EVENT_CLICKED ) {
        return( false );
    }
#endif

    const uint32_t now = millis();
    if ( last_event_ms != 0 && now - last_event_ms < 250 ) {
        return( false );
    }
    last_event_ms = now;
    return( true );
}

static int16_t osmmap_abs_i16( int16_t value ) {
    return( value < 0 ? (int16_t)-value : value );
}

static bool osmmap_obj_contains_touch_point( lv_obj_t *obj, int16_t x, int16_t y ) {
    if ( !obj || lv_obj_get_hidden( obj ) ) {
        return( false );
    }

    lv_area_t coords;
    lv_obj_get_coords( obj, &coords );

    return( x >= coords.x1 && x <= coords.x2 && y >= coords.y1 && y <= coords.y2 );
}

static bool osmmap_touch_starts_on_foreground_control( int16_t x, int16_t y ) {
    return(
        osmmap_obj_contains_touch_point( osmmap_layers_btn, x, y ) ||
        osmmap_obj_contains_touch_point( osmmap_exit_btn, x, y ) ||
        osmmap_obj_contains_touch_point( osmmap_zoom_in_btl, x, y ) ||
        osmmap_obj_contains_touch_point( osmmap_zoom_out_btl, x, y ) ||
        osmmap_obj_contains_touch_point( osmmap_sub_menu_layers, x, y ) ||
        osmmap_obj_contains_touch_point( osmmap_sub_menu_setting, x, y )
    );
}

static void osmmap_reset_touch_pan( void ) {
    osmmap_touch_pan_active = false;
    osmmap_touch_pan_consuming = false;
    osmmap_touch_pan_start_x = 0;
    osmmap_touch_pan_start_y = 0;
    osmmap_touch_pan_last_x = 0;
    osmmap_touch_pan_last_y = 0;
}

static bool osmmap_handle_touch_pan( const touch_t *touch ) {
    static uint32_t last_tile_nav_ms = 0;
    static const int16_t start_threshold = 10;
    static const int16_t move_threshold = 4;
    static const int16_t tile_nav_threshold = 26;

    if ( !osmmap_app_active || !osmmap_location || !touch ) {
        osmmap_reset_touch_pan();
        return( false );
    }

    if ( !touch->touched ) {
        const bool was_consuming = osmmap_touch_pan_consuming;

        osmmap_reset_touch_pan();
        return( was_consuming );
    }

    const int16_t x = touch->x_coor;
    const int16_t y = touch->y_coor;

    if ( !osmmap_touch_pan_active ) {
        if ( osmmap_touch_starts_on_foreground_control( x, y ) ) {
            osmmap_reset_touch_pan();
            return( false );
        }

        osmmap_touch_pan_active = true;
        osmmap_touch_pan_consuming = false;
        osmmap_touch_pan_start_x = x;
        osmmap_touch_pan_start_y = y;
        osmmap_touch_pan_last_x = x;
        osmmap_touch_pan_last_y = y;
        return( false );
    }

    const int16_t total_dx = x - osmmap_touch_pan_start_x;
    const int16_t total_dy = y - osmmap_touch_pan_start_y;
    const int16_t dx = x - osmmap_touch_pan_last_x;
    const int16_t dy = y - osmmap_touch_pan_last_y;

    if ( !osmmap_touch_pan_consuming ) {
        if ( osmmap_abs_i16( total_dx ) < start_threshold && osmmap_abs_i16( total_dy ) < start_threshold ) {
            return( false );
        }
        osmmap_touch_pan_consuming = true;
    }

    if ( osmmap_abs_i16( dx ) < move_threshold && osmmap_abs_i16( dy ) < move_threshold ) {
        return( true );
    }

    if ( osmmap_watch_flash_mode ) {
        osmmap_adjust_watch_flash_pan( dx, dy );
        osmmap_touch_pan_last_x = x;
        osmmap_touch_pan_last_y = y;
        return( true );
    }

    if ( osmmap_abs_i16( total_dx ) < tile_nav_threshold && osmmap_abs_i16( total_dy ) < tile_nav_threshold ) {
        return( true );
    }

    const uint32_t now = millis();
    if ( last_tile_nav_ms != 0 && now - last_tile_nav_ms < 150 ) {
        return( true );
    }
    last_tile_nav_ms = now;

    if ( osmmap_abs_i16( total_dx ) > osmmap_abs_i16( total_dy ) ) {
        osm_map_nav_direction( osmmap_location, total_dx > 0 ? west : east );
    }
    else {
        osm_map_nav_direction( osmmap_location, total_dy > 0 ? north : south );
    }

    osmmap_touch_pan_start_x = x;
    osmmap_touch_pan_start_y = y;
    osmmap_touch_pan_last_x = x;
    osmmap_touch_pan_last_y = y;
    if ( osmmap_app_active ) {
        osmmap_update_request();
    }
    return( true );
}

static bool osmmap_watch_flash_uses_current_tile( void ) {
    return( osmmap_watch_flash_mode && strstr( osmmap_watch_flash_uri, OSMMAP_WATCH_CURRENT_TILE_PATH ) != NULL );
}

static bool osmmap_marker_uses_image_transform( void ) {
#if defined( LILYGO_WATCH_ULTRA )
    return( true );
#else
    return( osmmap_watch_flash_mode );
#endif
}

static bool osmmap_watch_flash_pixel_to_view( double pixel_x, double pixel_y, uint16_t *x, uint16_t *y ) {
    if ( !x || !y || !std::isfinite( pixel_x ) || !std::isfinite( pixel_y ) ) {
        return( false );
    }
    if ( pixel_x < 0.0 || pixel_x >= 256.0 || pixel_y < 0.0 || pixel_y >= 256.0 ) {
        return( false );
    }

    lv_obj_t *parent = osmmap_overlay_layer ? osmmap_overlay_layer : ( osmmap_app_tile_img ? lv_obj_get_parent( osmmap_app_tile_img ) : NULL );
    const double dest_w = parent ? (double)lv_obj_get_width( parent ) : 240.0;
    const double dest_h = parent ? (double)lv_obj_get_height( parent ) : 240.0;

    if ( dest_w <= 0.0 || dest_h <= 0.0 ) {
        return( false );
    }

    const double scaled_x =
#if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )
        ( dest_w * 0.5 ) + ( pixel_x - 128.0 );
#else
        pixel_x * ( dest_w / 256.0 );
#endif
    const double scaled_y =
#if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )
        ( dest_h * 0.5 ) + ( pixel_y - 128.0 );
#else
        pixel_y * ( dest_h / 256.0 );
#endif

    *x = (uint16_t)lround( fmax( 0.0, fmin( dest_w - 1.0, scaled_x ) ) );
    *y = (uint16_t)lround( fmax( 0.0, fmin( dest_h - 1.0, scaled_y ) ) );
    return( true );
}

static bool osmmap_project_watch_flash_current_lon_lat( double lon, double lat, uint16_t *x, uint16_t *y ) {
    if ( !x || !y ) {
        return( false );
    }

    const uint32_t z = osmmap_watch_flash_projection_zoom;
    const double center_px_x = osmmap_lon2global_pixel_x( osmmap_watch_flash_center_lon, z );
    const double center_px_y = osmmap_lat2global_pixel_y( osmmap_watch_flash_center_lat, z );
    const double marker_px_x = osmmap_lon2global_pixel_x( lon, z );
    const double marker_px_y = osmmap_lat2global_pixel_y( lat, z );
    const double image_px_x = 128.0 + ( marker_px_x - center_px_x );
    const double image_px_y = 128.0 + ( marker_px_y - center_px_y );

    if ( !std::isfinite( image_px_x ) || !std::isfinite( image_px_y ) ) {
        return( false );
    }

    return( osmmap_watch_flash_pixel_to_view( image_px_x, image_px_y, x, y ) );
}

static bool osmmap_project_marker_lon_lat( double lon, double lat, uint16_t *x, uint16_t *y ) {
    if ( osmmap_watch_flash_uses_current_tile() ) {
        return( osmmap_project_watch_flash_current_lon_lat( lon, lat, x, y ) );
    }
#if defined( LILYGO_WATCH_ULTRA )
    uint16_t tile_x = 0;
    uint16_t tile_y = 0;

    if ( !osm_map_project_lon_lat( osmmap_location, lon, lat, &tile_x, &tile_y ) ) {
        return( false );
    }
    return( osmmap_watch_flash_pixel_to_view( (double)tile_x, (double)tile_y, x, y ) );
#else
    return( osm_map_project_lon_lat( osmmap_location, lon, lat, x, y ) );
#endif
}

static void osmmap_place_marker( lv_obj_t *marker_obj, uint16_t marker_x, uint16_t marker_y ) {
    lv_obj_t *parent = marker_obj ? lv_obj_get_parent( marker_obj ) : NULL;
    int32_t final_x = marker_x;
    int32_t final_y = marker_y;

    if ( !parent ) {
        return;
    }

    if ( osmmap_marker_uses_image_transform() && osmmap_app_tile_img ) {
        const int32_t center_x = lv_obj_get_width( parent ) / 2;
        const int32_t center_y = lv_obj_get_height( parent ) / 2;
        const int32_t pan_x = osmmap_watch_flash_mode ? osmmap_watch_flash_pan_x : 0;
        const int32_t pan_y = osmmap_watch_flash_mode ? osmmap_watch_flash_pan_y : 0;
        const double zoom_factor = (double)osmmap_get_display_lvgl_zoom() / 256.0;

        final_x = center_x + pan_x + (int32_t)lround( ( (double)marker_x - center_x ) * zoom_factor );
        final_y = center_y + pan_y + (int32_t)lround( ( (double)marker_y - center_y ) * zoom_factor );
    }

    const int32_t marker_width = lv_obj_get_width( marker_obj );
    const int32_t marker_height = lv_obj_get_height( marker_obj );

    lv_obj_set_pos( marker_obj, final_x - marker_width / 2, final_y - marker_height / 2 );
    lv_obj_set_hidden( marker_obj, false );
}

static void osmmap_refresh_marker_positions( void ) {
    if ( !osmmap_location ) {
        for ( size_t i = 0; i < OSMMAP_OVERLAY_MAX_ITEMS; i++ ) {
            osmmap_hide_overlay_marker( &osmmap_overlay_items[ i ] );
        }
        osmmap_update_watch_flash_status_label();
        return;
    }

    if ( osmmap_have_local_position && osmmap_app_pos_img ) {
        uint16_t marker_x = 0;
        uint16_t marker_y = 0;
        bool local_marker_placed = false;

        if ( osmmap_project_marker_lon_lat( osmmap_local_lon, osmmap_local_lat, &marker_x, &marker_y ) ) {
            osmmap_place_marker( osmmap_app_pos_img, marker_x, marker_y );
            local_marker_placed = true;
        }
        else if ( osmmap_location->tilexy_pos_valid &&
                  fabs( osmmap_local_lon - osmmap_location->lon ) < 0.0000001 &&
                  fabs( osmmap_local_lat - osmmap_location->lat ) < 0.0000001 ) {
            osmmap_place_marker( osmmap_app_pos_img, osmmap_location->tilex_pos, osmmap_location->tiley_pos );
            local_marker_placed = true;
        }
        else if ( fabs( osmmap_local_lon - osmmap_location->lon ) < 0.0000001 &&
                  fabs( osmmap_local_lat - osmmap_location->lat ) < 0.0000001 ) {
            osmmap_place_marker( osmmap_app_pos_img, osmmap_get_viewport_width() / 2, osmmap_get_viewport_height() / 2 );
            local_marker_placed = true;
        }

        if ( !local_marker_placed ) {
            lv_obj_set_hidden( osmmap_app_pos_img, true );
        }
        osmmap_local_marker_visible = local_marker_placed;
    }
    else if ( osmmap_app_pos_img ) {
        lv_obj_set_hidden( osmmap_app_pos_img, true );
        osmmap_local_marker_visible = false;
    }

    if ( osmmap_external_marker_valid ) {
        uint16_t marker_x = 0;
        uint16_t marker_y = 0;

        if ( osmmap_project_marker_lon_lat( osmmap_external_marker_lon, osmmap_external_marker_lat, &marker_x, &marker_y ) ) {
            osmmap_place_marker( osmmap_ext_pos_img, marker_x, marker_y );
        }
        else if ( osmmap_ext_pos_img ) {
            lv_obj_set_hidden( osmmap_ext_pos_img, true );
        }
    }
    else if ( osmmap_ext_pos_img ) {
        lv_obj_set_hidden( osmmap_ext_pos_img, true );
    }

    for ( size_t i = 0; i < OSMMAP_OVERLAY_MAX_ITEMS; i++ ) {
        osmmap_overlay_item_t *item = &osmmap_overlay_items[ i ];
        uint16_t marker_x = 0;
        uint16_t marker_y = 0;

        if ( !item->used ) {
            osmmap_hide_overlay_marker( item );
            continue;
        }
        if ( item->kind < 0 || item->kind >= OSMMAP_OVERLAY_KIND_COUNT ) {
            osmmap_hide_overlay_marker( item );
            continue;
        }
        if ( !osmmap_overlay_layer_enabled[ item->kind ] ) {
            osmmap_hide_overlay_marker( item );
            continue;
        }
        if ( osmmap_watch_flash_uses_current_tile() ) {
            if ( item->has_pixel && osmmap_watch_flash_pixel_to_view( item->pixel_x, item->pixel_y, &marker_x, &marker_y ) ) {
                // Host-projected pixel placement is authoritative for generated current.png maps.
            }
            else if ( !osmmap_project_marker_lon_lat( item->lon, item->lat, &marker_x, &marker_y ) ) {
                osmmap_hide_overlay_marker( item );
                continue;
            }
        }
        else if ( !osmmap_project_marker_lon_lat( item->lon, item->lat, &marker_x, &marker_y ) ) {
            osmmap_hide_overlay_marker( item );
            continue;
        }
        if ( osmmap_ensure_overlay_marker( item ) ) {
            osmmap_place_marker( item->marker_obj, marker_x, marker_y );
        }
    }
    osmmap_raise_local_position_marker();
    osmmap_update_gps_status_label();
}

static bool osmmap_adjust_watch_flash_zoom( int delta ) {
    const int next_zoom = (int)osmmap_watch_flash_render_zoom + delta;
    const int min_zoom = (int)osmmap_get_watch_flash_min_render_zoom();

    if ( !osmmap_watch_flash_mode ) {
        return( false );
    }

    if ( next_zoom < min_zoom || next_zoom > 18 ) {
        return( false );
    }

    osmmap_watch_flash_render_zoom = (uint32_t)next_zoom;
    osmmap_apply_image_zoom();
    return( true );
}

static bool osmmap_adjust_watch_flash_pan( int32_t delta_x, int32_t delta_y ) {
    const int32_t old_pan_x = osmmap_watch_flash_pan_x;
    const int32_t old_pan_y = osmmap_watch_flash_pan_y;

    if ( !osmmap_watch_flash_mode ) {
        return( false );
    }

    osmmap_watch_flash_pan_x += delta_x;
    osmmap_watch_flash_pan_y += delta_y;
    osmmap_clamp_watch_flash_pan();

    if ( old_pan_x == osmmap_watch_flash_pan_x && old_pan_y == osmmap_watch_flash_pan_y ) {
        return( false );
    }

    osmmap_apply_image_zoom();
    return( true );
}

void osmmap_app_main_setup( uint32_t tile_num ) {
    /**
     * load config
     */
    osmmap_config.load();
#if defined( LILYGO_WATCH_ULTRA )
    if ( osmmap_config.config_version < OSMMAP_CONFIG_VERSION ) {
        osmmap_config.wifi_autoon = false;
        osmmap_config.gps_on_standby = false;
        osmmap_config.config_version = OSMMAP_CONFIG_VERSION;
        osmmap_config.save();
    }
#endif
    osmmap_location = osm_map_create_location_obj();
    osmmap_location->load_ahead = osmmap_config.load_ahead;
    if ( osmmap_is_watch_flash_source_name( osmmap_config.osmmap ) ) {
        if ( osmmap_config.watch_flash_basemap_valid ) {
            osmmap_configure_watch_flash_source(
                osmmap_config.watch_flash_lon,
                osmmap_config.watch_flash_lat,
                osmmap_config.watch_flash_zoom,
                false
            );
        }
        else {
            struct stat seed_tile_stat;

            if ( stat( OSMMAP_WATCH_SEED_TILE_PATH, &seed_tile_stat ) == 0 ) {
                osmmap_configure_watch_flash_source( -81.70749, 43.74623, 10, true );
            }
        }
    }
#if defined( M5PAPER )
    osmmap_location->tilex_dest_px_res = 540;
    osmmap_location->tiley_dest_px_res = 540;
#elif defined( LILYGO_WATCH_ULTRA )
    osmmap_location->tilex_dest_px_res = 256;
    osmmap_location->tiley_dest_px_res = 256;
#endif
    const uint16_t osmmap_view_w = osmmap_get_viewport_width();
    const uint16_t osmmap_view_h = osmmap_get_viewport_height();
    /**
     * geht app tile
     */
    osmmap_app_main_tile = mainbar_get_tile_obj( tile_num );

    lv_style_copy( &osmmap_app_main_style, ws_get_mainbar_style() );
    lv_obj_add_style( osmmap_app_main_tile, LV_OBJ_PART_MAIN, &osmmap_app_main_style );

    const lv_color_t osmmap_control_icon_color = LV_COLOR_MAKE( 0xff, 0xd2, 0x00 );
    lv_style_copy( &osmmap_app_btn_style, ws_get_mainbar_style() );
    lv_style_set_image_recolor( &osmmap_app_btn_style, LV_OBJ_PART_MAIN, osmmap_control_icon_color );
    lv_style_set_image_recolor_opa( &osmmap_app_btn_style, LV_OBJ_PART_MAIN, LV_OPA_COVER );

    lv_style_copy( &osmmap_app_nav_style, ws_get_mainbar_style() );
    lv_style_set_radius( &osmmap_app_nav_style, LV_OBJ_PART_MAIN, 0 );
    lv_style_set_bg_color( &osmmap_app_nav_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );

    lv_style_copy( &osmmap_app_label_style, ws_get_mainbar_style() );
    lv_style_set_text_font( &osmmap_app_label_style, LV_OBJ_PART_MAIN, &Ubuntu_12px );
    lv_style_set_text_color(&osmmap_app_label_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );

    lv_obj_t *osmmap_cont = lv_obj_create( osmmap_app_main_tile, NULL );
    lv_obj_set_size( osmmap_cont, osmmap_view_w, osmmap_view_h );
    lv_obj_add_style( osmmap_cont, LV_OBJ_PART_MAIN, &osmmap_app_main_style );
    lv_obj_align( osmmap_cont, osmmap_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, 0 );

    osmmap_app_tile_img = lv_img_create( osmmap_cont, NULL );
    lv_obj_set_width( osmmap_app_tile_img, osmmap_view_w );
    lv_obj_set_height( osmmap_app_tile_img, osmmap_view_h );
    lv_img_set_src( osmmap_app_tile_img, osm_map_get_no_data_image() );
    lv_img_set_pivot( osmmap_app_tile_img, 128, 128 );
#ifdef M5PAPER
    lv_img_set_zoom( osmmap_app_tile_img, 540 );
#endif
    lv_obj_align( osmmap_app_tile_img, osmmap_cont, LV_ALIGN_CENTER, 0, 0 );
    osmmap_apply_image_zoom();

    osmmap_overlay_layer = lv_obj_create( osmmap_cont, NULL );
    lv_obj_set_size( osmmap_overlay_layer, osmmap_view_w, osmmap_view_h );
    lv_obj_set_click( osmmap_overlay_layer, false );
    lv_obj_set_style_local_bg_opa( osmmap_overlay_layer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP );
    lv_obj_set_style_local_border_width( osmmap_overlay_layer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_all( osmmap_overlay_layer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );
    lv_obj_align( osmmap_overlay_layer, osmmap_cont, LV_ALIGN_CENTER, 0, 0 );

    osmmap_create_local_position_marker( osmmap_overlay_layer );
    lv_obj_align( osmmap_app_pos_img, osmmap_overlay_layer, LV_ALIGN_IN_TOP_LEFT, 120, 120 );
    lv_obj_set_hidden( osmmap_app_pos_img, true );

    osmmap_ext_pos_img = lv_img_create( osmmap_overlay_layer, NULL );
    lv_img_set_src( osmmap_ext_pos_img, &info_ok_16px );
    lv_obj_set_click( osmmap_ext_pos_img, false );
    lv_obj_align( osmmap_ext_pos_img, osmmap_overlay_layer, LV_ALIGN_IN_TOP_LEFT, 120, 120 );
    lv_obj_set_hidden( osmmap_ext_pos_img, true );

#if !defined( LILYGO_WATCH_ULTRA )
    osmmap_lonlat_label = lv_label_create( osmmap_cont, NULL );
    lv_obj_add_style( osmmap_lonlat_label, LV_OBJ_PART_MAIN, &osmmap_app_label_style );
    lv_obj_align( osmmap_lonlat_label, osmmap_cont, LV_ALIGN_IN_TOP_LEFT, 3, 0 );
    lv_label_set_text( osmmap_lonlat_label, "0 / 0" );
#endif

    #if defined( LILYGO_WATCH_ULTRA )
        osmmap_layers_btn = wf_add_image_button( osmmap_cont, layers_dark_48px, layers_btn_app_main_event_cb, &osmmap_app_btn_style );
    #else
        osmmap_layers_btn = wf_add_menu_button( osmmap_cont, layers_btn_app_main_event_cb, &osmmap_app_btn_style );
    #endif
    lv_obj_align( osmmap_layers_btn, osmmap_cont, LV_ALIGN_IN_TOP_LEFT, THEME_PADDING, THEME_PADDING );

    #if defined( LILYGO_WATCH_ULTRA )
        osmmap_exit_btn = wf_add_image_button( osmmap_cont, exit_dark_48px, exit_osmmap_app_main_event_cb, &osmmap_app_btn_style );
    #else
        osmmap_exit_btn = wf_add_exit_button( osmmap_cont, exit_osmmap_app_main_event_cb, &osmmap_app_btn_style );
    #endif
    lv_obj_align( osmmap_exit_btn, osmmap_cont, LV_ALIGN_IN_BOTTOM_LEFT, THEME_PADDING, -THEME_PADDING );

    #if defined( LILYGO_WATCH_ULTRA )
        osmmap_zoom_in_btl = wf_add_image_button( osmmap_cont, zoom_in_dark_48px, zoom_in_osmmap_app_main_event_cb, &osmmap_app_btn_style );
    #else
        osmmap_zoom_in_btl = wf_add_zoom_in_button( osmmap_cont, zoom_in_osmmap_app_main_event_cb, &osmmap_app_btn_style );
    #endif
    lv_obj_align( osmmap_zoom_in_btl, osmmap_cont, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, THEME_PADDING );

    #if defined( LILYGO_WATCH_ULTRA )
        osmmap_zoom_out_btl = wf_add_image_button( osmmap_cont, zoom_out_dark_48px, zoom_out_osmmap_app_main_event_cb, &osmmap_app_btn_style );
    #else
        osmmap_zoom_out_btl = wf_add_zoom_out_button( osmmap_cont, zoom_out_osmmap_app_main_event_cb, &osmmap_app_btn_style );
    #endif
    lv_obj_align( osmmap_zoom_out_btl, osmmap_cont, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_PADDING, -THEME_PADDING );

    osmmap_north_btn = lv_btn_create( osmmap_cont, NULL );
    lv_obj_set_width( osmmap_north_btn, 80 );
    lv_obj_set_height( osmmap_north_btn, 48 );
    lv_obj_set_click( osmmap_north_btn, false );
    lv_obj_add_protect( osmmap_north_btn, LV_PROTECT_CLICK_FOCUS );
    lv_obj_add_style( osmmap_north_btn, LV_BTN_PART_MAIN, &osmmap_app_nav_style );
    lv_obj_align( osmmap_north_btn, osmmap_cont, LV_ALIGN_IN_TOP_MID, 0, 0 );
    lv_obj_set_event_cb( osmmap_north_btn, nav_direction_osmmap_app_main_event_cb );

    osmmap_south_btn = lv_btn_create( osmmap_cont, osmmap_north_btn );
    lv_obj_set_click( osmmap_south_btn, false );
    lv_obj_align( osmmap_south_btn, osmmap_cont, LV_ALIGN_IN_BOTTOM_MID, 0, 0 );
    lv_obj_set_event_cb( osmmap_south_btn, nav_direction_osmmap_app_main_event_cb );

    osmmap_west_btn = lv_btn_create( osmmap_cont, NULL );
    lv_obj_set_width( osmmap_west_btn, 48 );
    lv_obj_set_height( osmmap_west_btn, 80 );
    lv_obj_set_click( osmmap_west_btn, false );
    lv_obj_add_protect( osmmap_west_btn, LV_PROTECT_CLICK_FOCUS );
    lv_obj_add_style( osmmap_west_btn, LV_BTN_PART_MAIN, &osmmap_app_nav_style );
    lv_obj_align( osmmap_west_btn, osmmap_cont, LV_ALIGN_IN_LEFT_MID, 0, 0 );
    lv_obj_set_event_cb( osmmap_west_btn, nav_direction_osmmap_app_main_event_cb );

    osmmap_east_btn = lv_btn_create( osmmap_cont, osmmap_west_btn );
    lv_obj_set_click( osmmap_east_btn, false );
    lv_obj_align( osmmap_east_btn, osmmap_cont, LV_ALIGN_IN_RIGHT_MID, 0, 0 );
    lv_obj_set_event_cb( osmmap_east_btn, nav_direction_osmmap_app_main_event_cb );

    osmmap_zoom_northwest_btn = lv_btn_create( osmmap_cont, NULL );
    lv_obj_set_width( osmmap_zoom_northwest_btn, 72 );
    lv_obj_set_height( osmmap_zoom_northwest_btn, 72 );
    lv_obj_set_click( osmmap_zoom_northwest_btn, false );
    lv_obj_add_protect( osmmap_zoom_northwest_btn, LV_PROTECT_CLICK_FOCUS );
    lv_imgbtn_set_checkable( osmmap_zoom_northwest_btn, true );
    lv_obj_add_style( osmmap_zoom_northwest_btn, LV_BTN_PART_MAIN, &osmmap_app_nav_style );
    lv_obj_align( osmmap_zoom_northwest_btn, osmmap_cont, LV_ALIGN_CENTER, -36, -36 );
    lv_obj_set_event_cb( osmmap_zoom_northwest_btn, nav_center_osmmap_app_main_event_cb );

    osmmap_zoom_northeast_btn = lv_btn_create( osmmap_cont, osmmap_zoom_northwest_btn );
    lv_obj_set_click( osmmap_zoom_northeast_btn, false );
    lv_obj_align( osmmap_zoom_northeast_btn, osmmap_cont, LV_ALIGN_CENTER, 36, -36 );
    lv_obj_set_event_cb( osmmap_zoom_northeast_btn, nav_center_osmmap_app_main_event_cb );

    osmmap_zoom_southwest_btn = lv_btn_create( osmmap_cont, osmmap_zoom_northwest_btn );
    lv_obj_set_click( osmmap_zoom_southwest_btn, false );
    lv_obj_align( osmmap_zoom_southwest_btn, osmmap_cont, LV_ALIGN_CENTER, -36, 36 );
    lv_obj_set_event_cb( osmmap_zoom_southwest_btn, nav_center_osmmap_app_main_event_cb );

    osmmap_zoom_southeast_btn = lv_btn_create( osmmap_cont, osmmap_zoom_northwest_btn );
    lv_obj_set_click( osmmap_zoom_southeast_btn, false );
    lv_obj_align( osmmap_zoom_southeast_btn, osmmap_cont, LV_ALIGN_CENTER, 36, 36 );
    lv_obj_set_event_cb( osmmap_zoom_southeast_btn, nav_center_osmmap_app_main_event_cb );
    /**
     * setup menu
     */
    osmmap_sub_menu_layers = lv_list_create( osmmap_cont, NULL );
    lv_obj_set_size( osmmap_sub_menu_layers, 160, 200 );
    lv_obj_align( osmmap_sub_menu_layers, osmmap_cont, LV_ALIGN_IN_RIGHT_MID, 0, 0);
    osmmap_add_tile_server_list( osmmap_sub_menu_layers );
    lv_obj_set_hidden( osmmap_sub_menu_layers, true );

    osmmap_sub_menu_setting = lv_list_create( osmmap_cont, NULL );
    lv_obj_set_size( osmmap_sub_menu_setting, 160, 200 );
    lv_obj_align( osmmap_sub_menu_setting, osmmap_cont, LV_ALIGN_IN_RIGHT_MID, 0, 0);
    osmmap_app_set_setting_menu( osmmap_sub_menu_setting );
    lv_obj_set_hidden( osmmap_sub_menu_setting, true );
    /**
     * set left/right hand mode
     */
    osmmap_app_set_left_right_hand( osmmap_config.left_right_hand );
    osmmap_raise_primary_controls();
    osmmap_load_overlay_items();
    /**
     * setup event callback and background Task
     */
    mainbar_add_tile_activate_cb( tile_num, osmmap_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, osmmap_hibernate_cb );
    mainbar_add_tile_button_cb( tile_num, osmmap_button_cb );
    gpsctl_register_cb(
        GPSCTL_ENABLE |
        GPSCTL_DISABLE |
        GPSCTL_FIX |
        GPSCTL_NOFIX |
        GPSCTL_SET_APP_LOCATION |
        GPSCTL_UPDATE_LOCATION |
        GPSCTL_UPDATE_COURSE |
        GPSCTL_UPDATE_SATELLITE,
        osmmap_gpsctl_event_cb,
        "osm"
    );
    touch_register_cb( TOUCH_UPDATE , osmmap_app_touch_event_cb, "osm touch" );
#ifdef NATIVE_64BIT
    eventmask = 0;
#else
    osmmap_event_handle = xEventGroupCreate();
#endif
    osmmap_main_tile_task = lv_task_create( osmmap_main_tile_update_task, 250, LV_TASK_PRIO_MID, NULL );
}

bool osmmap_app_touch_event_cb( EventBits_t event, void *arg ) {
    bool handled = false;

    switch( event ) {
        case( TOUCH_UPDATE ):
            if ( osmmap_app_active ) {
                last_touch = millis();
                handled = osmmap_handle_touch_pan( (const touch_t *)arg );
            }
            break;
    }
    return( handled );
}

bool osmmap_button_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case BUTTON_LEFT:   if ( osmmap_watch_flash_mode ) {
                                osmmap_adjust_watch_flash_zoom( 1 );
                                break;
                            }
                            osm_map_zoom_in( osmmap_location );
                            if ( osmmap_app_active )
                                osmmap_update_request();
                            break;
        case BUTTON_RIGHT:  if ( osmmap_watch_flash_mode ) {
                                osmmap_adjust_watch_flash_zoom( -1 );
                                break;
                            }
                            osm_map_zoom_out( osmmap_location );
                            if ( osmmap_app_active )
                                osmmap_update_request();
                            break;
    }
    return( true );
}

void osmmap_app_set_left_right_hand( bool left_right_hand ) {
#if defined( LILYGO_WATCH_ULTRA )
    const lv_coord_t control_pad = 22;
#else
    const lv_coord_t control_pad = 10;
#endif

    if ( left_right_hand ) {
        lv_obj_align( osmmap_layers_btn, lv_obj_get_parent( osmmap_layers_btn ), LV_ALIGN_IN_TOP_RIGHT, -control_pad, control_pad );
        lv_obj_align( osmmap_exit_btn, lv_obj_get_parent( osmmap_exit_btn ), LV_ALIGN_IN_BOTTOM_RIGHT, -control_pad, -control_pad );
        lv_obj_align( osmmap_zoom_in_btl, lv_obj_get_parent( osmmap_zoom_in_btl ), LV_ALIGN_IN_TOP_LEFT, control_pad, control_pad );
        lv_obj_align( osmmap_zoom_out_btl, lv_obj_get_parent( osmmap_zoom_out_btl ), LV_ALIGN_IN_BOTTOM_LEFT, control_pad, -control_pad );
        lv_obj_align( osmmap_sub_menu_layers, lv_obj_get_parent( osmmap_sub_menu_layers ), LV_ALIGN_IN_LEFT_MID, 0, 0);
        lv_obj_align( osmmap_sub_menu_setting, lv_obj_get_parent( osmmap_sub_menu_setting ), LV_ALIGN_IN_LEFT_MID, 0, 0);
    }
    else {
        lv_obj_align( osmmap_layers_btn, lv_obj_get_parent( osmmap_layers_btn ), LV_ALIGN_IN_TOP_LEFT, control_pad, control_pad );
        lv_obj_align( osmmap_exit_btn, lv_obj_get_parent( osmmap_exit_btn ), LV_ALIGN_IN_BOTTOM_LEFT, control_pad, -control_pad );
        lv_obj_align( osmmap_zoom_in_btl, lv_obj_get_parent( osmmap_zoom_in_btl ), LV_ALIGN_IN_TOP_RIGHT, -control_pad, control_pad );
        lv_obj_align( osmmap_zoom_out_btl, lv_obj_get_parent( osmmap_zoom_out_btl ), LV_ALIGN_IN_BOTTOM_RIGHT, -control_pad, -control_pad );
        lv_obj_align( osmmap_sub_menu_layers, lv_obj_get_parent( osmmap_sub_menu_layers ), LV_ALIGN_IN_RIGHT_MID, 0, 0);
        lv_obj_align( osmmap_sub_menu_setting, lv_obj_get_parent( osmmap_sub_menu_setting ), LV_ALIGN_IN_RIGHT_MID, 0, 0);
    }
    osmmap_raise_primary_controls();
}

void osmmap_app_set_setting_menu( lv_obj_t *menu ) {
    lv_obj_t * menu_entry;
    char cachestring[32] = "";

    if ( menu ) {
        /**
         * clear all menu entrys
         */
        while ( lv_list_remove( menu, 0 ) );
        /**
         * add menu entry
         */
        menu_entry = lv_list_add_btn( menu, NULL, "OSM maps" );
        lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        menu_entry = lv_list_add_btn( menu, NULL, "left/right hand" );
        lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        menu_entry = lv_list_add_btn( menu, osmmap_config.gps_autoon ? &checked_dark_16px : &unchecked_dark_16px, "autostart gps" );
        lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        menu_entry = lv_list_add_btn( menu, osmmap_config.gps_on_standby ? &checked_dark_16px : &unchecked_dark_16px, "gps on standby" );
        lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        menu_entry = lv_list_add_btn( menu, osmmap_config.wifi_autoon ? &checked_dark_16px : &unchecked_dark_16px, "autostart wifi" );
        lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        menu_entry = lv_list_add_btn( menu, osmmap_config.load_ahead ? &checked_dark_16px : &unchecked_dark_16px, "load ahead" );
        lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        snprintf( cachestring, sizeof( cachestring ), "%dkB cached", osm_map_get_used_cache_size( osmmap_location ) / 1024 );
        menu_entry = lv_list_add_btn( menu, NULL, cachestring );
        lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        for ( int i = 0; i < (int)OSMMAP_OVERLAY_KIND_COUNT; i++ ) {
            menu_entry = lv_list_add_btn(
                menu,
                osmmap_overlay_layer_enabled[ i ] ? &checked_dark_16px : &unchecked_dark_16px,
                osmmap_overlay_menu_label( (osmmap_overlay_kind_t)i )
            );
            lv_obj_set_event_cb( menu_entry, osmmap_app_get_setting_menu_cb );
        }
    }
}

static void osmmap_app_get_setting_menu_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):
            if ( !strcmp( lv_list_get_btn_text( obj ), "OSM maps") ) {
                lv_obj_set_hidden( osmmap_sub_menu_setting, true );
                lv_obj_set_hidden( osmmap_sub_menu_layers, false );
            }
            else if ( !strcmp( lv_list_get_btn_text( obj ), "load ahead" ) ) {
                osmmap_config.load_ahead = !osmmap_config.load_ahead;
                osmmap_location->load_ahead = osmmap_config.load_ahead;
                osmmap_config.save();
            }
            else if ( !strcmp( lv_list_get_btn_text( obj ), "autostart gps" ) ) {
                osmmap_config.gps_autoon = !osmmap_config.gps_autoon;
                gpsctl_set_autoon( osmmap_config.gps_autoon );
                osmmap_config.save();
            }
            else if ( !strcmp( lv_list_get_btn_text( obj ), "gps on standby" ) ) {
                osmmap_config.gps_on_standby = !osmmap_config.gps_on_standby;
                gpsctl_set_enable_on_standby( osmmap_config.gps_on_standby );
                osmmap_config.save();
            }
            else if ( !strcmp( lv_list_get_btn_text( obj ), "autostart wifi" ) ) {
                osmmap_config.wifi_autoon = !osmmap_config.wifi_autoon;
                wifictl_set_autoon( osmmap_config.wifi_autoon );
                osmmap_config.save();
            }
            else if ( !strcmp( lv_list_get_btn_text( obj ), "left/right hand" ) ) {
                osmmap_config.left_right_hand = !osmmap_config.left_right_hand;
                osmmap_app_set_left_right_hand( osmmap_config.left_right_hand );
                osmmap_config.save();
            }
            else {
                const osmmap_overlay_kind_t kind = osmmap_overlay_kind_from_menu_label( lv_list_get_btn_text( obj ) );

                if ( kind != OSMMAP_OVERLAY_KIND_UNKNOWN ) {
                    osmmap_overlay_layer_enabled[ kind ] = !osmmap_overlay_layer_enabled[ kind ];
                    osmmap_refresh_marker_positions();
                }
            }
            osmmap_app_set_setting_menu( osmmap_sub_menu_setting );
            break;
    }
}

/**
 * @brief when osm is active, this task get the use inactive time and hide
 * the statusbar and icon.
 */
void osmmap_main_tile_update_task( lv_task_t * task ) {
    /*
     * check if maintile alread initialized
     */
/*
    if ( osmmap_app_active ) {
        if ( last_touch + 5000 < millis() ) {
            lv_obj_set_hidden( osmmap_layers_btn, true );
            lv_obj_set_hidden( osmmap_exit_btn, true );
            lv_obj_set_hidden( osmmap_zoom_in_btl, true );
            lv_obj_set_hidden( osmmap_zoom_out_btl, true );
            lv_obj_set_hidden( osmmap_zoom_northwest_btn, true );
            lv_obj_set_hidden( osmmap_zoom_northeast_btn, true );
            lv_obj_set_hidden( osmmap_zoom_southwest_btn, true );
            lv_obj_set_hidden( osmmap_zoom_southeast_btn, true );
            lv_obj_set_hidden( osmmap_north_btn, true );
            lv_obj_set_hidden( osmmap_south_btn, true );
            lv_obj_set_hidden( osmmap_west_btn, true );
            lv_obj_set_hidden( osmmap_east_btn, true );
            lv_obj_set_hidden( osmmap_sub_menu_layers, true );
            lv_obj_set_hidden( osmmap_sub_menu_setting, true );
        }
        else {
            lv_obj_set_hidden( osmmap_layers_btn, false );
            lv_obj_set_hidden( osmmap_exit_btn, false );
            lv_obj_set_hidden( osmmap_zoom_in_btl, false );
            lv_obj_set_hidden( osmmap_zoom_out_btl, false );
            lv_obj_set_hidden( osmmap_zoom_northwest_btn, false );
            lv_obj_set_hidden( osmmap_zoom_northeast_btn, false );
            lv_obj_set_hidden( osmmap_zoom_southwest_btn, false );
            lv_obj_set_hidden( osmmap_zoom_southeast_btn, false );
            lv_obj_set_hidden( osmmap_north_btn, false );
            lv_obj_set_hidden( osmmap_south_btn, false );
            lv_obj_set_hidden( osmmap_west_btn, false );
            lv_obj_set_hidden( osmmap_east_btn, false );
        }
    }
*/
#ifdef NATIVE_64BIT
    osmmap_load_ahead_Task( NULL );
    osmmap_update_Task( NULL );
#endif
}

bool osmmap_gpsctl_event_cb( EventBits_t event, void *arg ) {
    gps_data_t *gps_data = NULL;
    char lonlat[64] = "";
    
    switch ( event ) {
        case GPSCTL_ENABLE:
            osmmap_gps_enabled_seen = true;
            osmmap_update_gps_status_label();
            break;
        case GPSCTL_DISABLE:
            osmmap_gps_enabled_seen = false;
            osmmap_gps_fix_seen = false;
            osmmap_have_local_position = false;
            osmmap_have_local_heading = false;
            osmmap_gps_satellites = 0;
            osmmap_refresh_marker_positions();
            break;
        case GPSCTL_FIX:
            osmmap_gps_enabled_seen = true;
            osmmap_gps_fix_seen = true;
            osmmap_update_gps_status_label();
            break;
        case GPSCTL_NOFIX:
            osmmap_gps_enabled_seen = true;
            osmmap_gps_fix_seen = false;
            osmmap_update_gps_status_label();
            break;
        case GPSCTL_SET_APP_LOCATION:
            /**
             * update location and tile map image on new location
             */
            OSMMAP_APP_LOG("get new gps coor.");
            gps_data = ( gps_data_t *)arg;
            if ( !gps_data ) {
                break;
            }
            osmmap_gps_enabled_seen = true;
            osmmap_gps_fix_seen = true;
            osm_map_set_lon_lat( osmmap_location, gps_data->lon, gps_data->lat );
            osmmap_set_local_position_from_gps( gps_data );
            osmmap_set_local_heading_from_gps( gps_data );
            snprintf( lonlat, sizeof( lonlat ), "%f° / %f°", gps_data->lat, gps_data->lon );
            if ( osmmap_watch_flash_mode ) {
                osmmap_update_watch_flash_status_label();
            }
            else if ( osmmap_lonlat_label ) {
                lv_label_set_text( osmmap_lonlat_label, (const char*)lonlat );
            }
            osmmap_have_local_position = true;
            if ( osmmap_app_active )
                osmmap_update_request();
            break;
        case GPSCTL_UPDATE_LOCATION:
            /**
             * Move the local GPS marker. Do not request a map/tile refresh for
             * every live GPS tick on Ultra; that makes touch lag badly.
             */
            OSMMAP_APP_LOG("get new gps coor.");
            gps_data = ( gps_data_t *)arg;
            if ( !gps_data ) {
                break;
            }
            osmmap_gps_enabled_seen = true;
            osmmap_gps_fix_seen = true;
        #if defined( LILYGO_WATCH_ULTRA )
            {
                const uint32_t now = millis();
                const bool marker_changed =
                    !osmmap_have_local_position ||
                    fabs( gps_data->lon - osmmap_local_lon ) > 0.00001 ||
                    fabs( gps_data->lat - osmmap_local_lat ) > 0.00001;

                osmmap_set_local_position_from_gps( gps_data );
                osmmap_set_local_heading_from_gps( gps_data );

                if ( osmmap_app_active &&
                     ( marker_changed || (uint32_t)( now - osmmap_last_gps_marker_refresh_ms ) >= 1000 ) ) {
                    osmmap_refresh_local_position_marker();
                    osmmap_last_gps_marker_refresh_ms = now;
                }
                else if ( !osmmap_app_active ) {
                    osmmap_update_gps_status_label();
                }
            }
        #else
            osm_map_set_lon_lat( osmmap_location, gps_data->lon, gps_data->lat );
            osmmap_set_local_position_from_gps( gps_data );
            osmmap_set_local_heading_from_gps( gps_data );
            snprintf( lonlat, sizeof( lonlat ), "%f° / %f°", gps_data->lat, gps_data->lon );
            if ( osmmap_watch_flash_mode ) {
                osmmap_update_watch_flash_status_label();
            }
            else if ( osmmap_lonlat_label ) {
                lv_label_set_text( osmmap_lonlat_label, (const char*)lonlat );
            }
            osmmap_have_local_position = true;
            if ( osmmap_app_active )
                osmmap_update_request();
        #endif
            break;
        case GPSCTL_UPDATE_COURSE:
            gps_data = ( gps_data_t *)arg;
            osmmap_set_local_heading_from_gps( gps_data );
        #if defined( LILYGO_WATCH_ULTRA )
            if ( osmmap_app_active ) {
                const uint32_t now = millis();
                if ( (uint32_t)( now - osmmap_last_gps_marker_refresh_ms ) >= 1000 ) {
                    osmmap_refresh_local_position_marker();
                    osmmap_last_gps_marker_refresh_ms = now;
                }
            }
        #else
            osmmap_refresh_marker_positions();
        #endif
            break;
        case GPSCTL_UPDATE_SATELLITE:
            gps_data = ( gps_data_t *)arg;
            if ( gps_data && gps_data->valid_satellite ) {
                osmmap_gps_satellites = gps_data->satellites;
            }
            else {
                osmmap_gps_satellites = 0;
            }
            osmmap_update_gps_status_label();
            break;
    }
    return( true );
}


void osmmap_update_request( void ) {
    /**
     * check if another osm tile image update is running
     */
#ifdef NATIVE_64BIT
    if ( eventmask & OSM_APP_UPDATE_REQUEST ) {
        return;
    }
    else {
        eventmask |= OSM_APP_UPDATE_REQUEST;
    }
#else
    if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_UPDATE_REQUEST ) {
        return;
    }
    else {
        xEventGroupSetBits( osmmap_event_handle, OSM_APP_UPDATE_REQUEST );
    }
#endif
}

void osmmap_load_ahead_Task( void * pvParameters ) {
#ifdef NATIVE_64BIT
    /**
     * check for  load ahead request
     */
    if ( eventmask & OSM_APP_LOAD_AHEAD_REQUEST ) {
        /**
         * check if load ahead need or finsh
         */
        OSMMAP_APP_LOG("start load ahead update handler");
        eventmask &= ~OSM_APP_LOAD_AHEAD_REQUEST ;
        if ( osmmap_watch_flash_uses_current_tile() ) {
            return;
        }
        while ( osm_map_load_tiles_ahead( osmmap_location ) ) {}
    }
#else
    OSMMAP_APP_INFO_LOG("start osm map load ahead background task, heap: %d", ESP.getFreeHeap() );
    while( true ) {
        /**
         * check for  load ahead request
         */
        if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_LOAD_AHEAD_REQUEST ) {
            /**
             * check if load ahead need or finsh
             */
            OSMMAP_APP_LOG("start load ahead update handler");
            xEventGroupClearBits( osmmap_event_handle, OSM_APP_LOAD_AHEAD_REQUEST );
            if ( osmmap_watch_flash_uses_current_tile() ) {
                continue;
            }
            while ( osm_map_load_tiles_ahead( osmmap_location ) ) {
                /**
                 * block this task for 125ms
                 */
                vTaskDelay( 25 );
            }
        }
        /**
         * check if for a task exit request
         */
        if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_TASK_EXIT_REQUEST ) {
            break;
        }
        /**
         * block this task for 125ms
         */
        vTaskDelay( 25 );
    }
    OSMMAP_APP_INFO_LOG("finsh osm map load ahead background task, heap: %d", ESP.getFreeHeap() );
    vTaskDelete( NULL );    
#endif
}

void osmmap_update_Task( void * pvParameters ) {
#ifdef NATIVE_64BIT
    /**
     * check if a tile image update is requested
     */
    if ( eventmask & OSM_APP_UPDATE_REQUEST ) {
        /**
         * check if a tile image update is required and update them
         */
        OSMMAP_APP_LOG("start osm map update");
        if ( osmmap_watch_flash_uses_current_tile() ) {
            osmmap_load_watch_flash_image( false );
            osmmap_apply_image_zoom();
        }
        else if( osm_map_update( osmmap_location ) ) {
            if ( osm_map_get_tile_image( osmmap_location ) ) {
                lv_img_set_src( osmmap_app_tile_img, osm_map_get_tile_image( osmmap_location ) );
            }
            osmmap_apply_image_zoom();
            eventmask |= OSM_APP_LOAD_AHEAD_REQUEST;
        }
        osmmap_refresh_marker_positions();
        /**
         * clear update request flag
         */
        eventmask &= ~OSM_APP_UPDATE_REQUEST;
    }
#else
    OSMMAP_APP_INFO_LOG("start osm map tile background update task, heap: %d", ESP.getFreeHeap() );
    while( true ) {
        /**
         * check if a tile image update is requested
         */
        if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_UPDATE_REQUEST ) {
            /**
             * check if a tile image update is required and update them
             */
            OSMMAP_APP_LOG("start osm map update");
            if ( osmmap_watch_flash_uses_current_tile() ) {
                gui_take();
                osmmap_load_watch_flash_image( false );
                osmmap_apply_image_zoom();
                gui_give();
            }
            else if( osm_map_update( osmmap_location ) ) {
                gui_take();
                if ( osm_map_get_tile_image( osmmap_location ) ) {
                    lv_img_set_src( osmmap_app_tile_img, osm_map_get_tile_image( osmmap_location ) );
                }
                osmmap_apply_image_zoom();
                gui_give();
                xEventGroupSetBits( osmmap_event_handle, OSM_APP_LOAD_AHEAD_REQUEST );
            }
            gui_take();
            osmmap_refresh_marker_positions();
            gui_give();
            /**
             * clear update request flag
             */
            xEventGroupClearBits( osmmap_event_handle, OSM_APP_UPDATE_REQUEST );
        }
        /**
         * check if for a task exit request
         */
        if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_TASK_EXIT_REQUEST ) {
            OSMMAP_APP_INFO_LOG("stop osm map update task");
            break;
        }
        /**
         * block this task for 125ms
         */
        vTaskDelay( 25 );
    }
    OSMMAP_APP_INFO_LOG("finsh osm map tile background update task, heap: %d", ESP.getFreeHeap() );
    vTaskDelete( NULL );    
#endif
}

static void exit_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    static uint32_t last_event_ms = 0;
    if ( !osmmap_accept_primary_control_event( event, last_event_ms ) ) {
        return;
    }

    mainbar_jump_back();
}

static void nav_direction_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        /**
         * long press event for center
         */
        case( LV_EVENT_LONG_PRESSED ):
            if ( osmmap_watch_flash_mode ) {
                osmmap_watch_flash_pan_x = 0;
                osmmap_watch_flash_pan_y = 0;
                osmmap_apply_image_zoom();
                break;
            }
            OSMMAP_APP_LOG("center map to pos");
            osm_map_center_location( osmmap_location );
            osmmap_update_request();
            break;
        /**
         * short press event to move
         */   
        case( LV_EVENT_SHORT_CLICKED ):
            if ( osmmap_watch_flash_mode ) {
                const int32_t pan_step = 40;

                if ( obj == osmmap_north_btn ) {
                    osmmap_adjust_watch_flash_pan( 0, -pan_step );
                }
                else if ( obj == osmmap_south_btn ) {
                    osmmap_adjust_watch_flash_pan( 0, pan_step );
                }
                else if ( obj == osmmap_west_btn ) {
                    osmmap_adjust_watch_flash_pan( -pan_step, 0 );
                }
                else if ( obj == osmmap_east_btn ) {
                    osmmap_adjust_watch_flash_pan( pan_step, 0 );
                }
                break;
            }
            if ( obj == osmmap_north_btn ) {
                OSMMAP_APP_LOG("nav north direction");
                osm_map_nav_direction( osmmap_location, north );
            }
            else if ( obj == osmmap_south_btn ) {
                OSMMAP_APP_LOG("nav south direction");
                osm_map_nav_direction( osmmap_location, south );
            }
            else if ( obj == osmmap_west_btn ) {
                OSMMAP_APP_LOG("nav west direction");
                osm_map_nav_direction( osmmap_location, west );
            }
            else if ( obj == osmmap_east_btn ) {
                OSMMAP_APP_LOG("nav east direction");
                osm_map_nav_direction( osmmap_location, east );
            }
            else {
                OSMMAP_APP_LOG("direction source unknown");
            }
            if ( osmmap_app_active )
                osmmap_update_request();
            break;
    }
}

static void nav_center_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        /**
         * long press event for center
         */
        case( LV_EVENT_LONG_PRESSED ):
            if ( osmmap_watch_flash_mode ) {
                osmmap_watch_flash_pan_x = 0;
                osmmap_watch_flash_pan_y = 0;
                osmmap_apply_image_zoom();
                break;
            }
            OSMMAP_APP_LOG("center map to pos");
            osm_map_center_location( osmmap_location );
            osmmap_update_request();
            break;
        /**
         * short press event to zoom in
         */
        case( LV_EVENT_SHORT_CLICKED ):
            if ( osmmap_watch_flash_mode ) {
                const int32_t pan_step = 28;

                if ( obj == osmmap_zoom_northwest_btn ) {
                    osmmap_adjust_watch_flash_pan( -pan_step, -pan_step );
                }
                else if ( obj == osmmap_zoom_northeast_btn ) {
                    osmmap_adjust_watch_flash_pan( pan_step, -pan_step );
                }
                else if ( obj == osmmap_zoom_southwest_btn ) {
                    osmmap_adjust_watch_flash_pan( -pan_step, pan_step );
                }
                else if ( obj == osmmap_zoom_southeast_btn ) {
                    osmmap_adjust_watch_flash_pan( pan_step, pan_step );
                }
                break;
            }
            if ( obj == osmmap_zoom_northwest_btn ) {
                OSMMAP_APP_LOG("nav northwest center");
                osm_map_nav_direction( osmmap_location, zoom_northwest );
            }
            else if ( obj == osmmap_zoom_northeast_btn ) {
                OSMMAP_APP_LOG("nav northeast center");
                osm_map_nav_direction( osmmap_location, zoom_northeast );
            }
            else if ( obj == osmmap_zoom_southwest_btn ) {
                OSMMAP_APP_LOG("nav southwest center");
                osm_map_nav_direction( osmmap_location, zoom_southwest );
            }
            else if ( obj == osmmap_zoom_southeast_btn ) {
                OSMMAP_APP_LOG("nav southeast center");
                osm_map_nav_direction( osmmap_location, zoom_southeast );
            }
            else {
                OSMMAP_APP_LOG("zoom source unknown");
                osm_map_nav_direction( osmmap_location, east );
            }
            if ( osmmap_app_active )
                osmmap_update_request();
            break;
    }
}

static void zoom_in_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    static uint32_t last_event_ms = 0;
    if ( !osmmap_accept_primary_control_event( event, last_event_ms ) ) {
        return;
    }

    if ( osmmap_watch_flash_mode ) {
        osmmap_adjust_watch_flash_zoom( 1 );
        return;
    }
    osm_map_zoom_in( osmmap_location );
    if ( osmmap_app_active )
        osmmap_update_request();
}

static void zoom_out_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    static uint32_t last_event_ms = 0;
    if ( !osmmap_accept_primary_control_event( event, last_event_ms ) ) {
        return;
    }

    if ( osmmap_watch_flash_mode ) {
        osmmap_adjust_watch_flash_zoom( -1 );
        return;
    }
    osm_map_zoom_out( osmmap_location );
    if ( osmmap_app_active )
        osmmap_update_request();
}

static void layers_btn_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    static uint32_t last_event_ms = 0;
    if ( !osmmap_accept_primary_control_event( event, last_event_ms ) ) {
        return;
    }

    if ( lv_obj_get_hidden( osmmap_sub_menu_setting ) ) {
        osmmap_app_set_setting_menu( osmmap_sub_menu_setting );
        lv_obj_set_hidden( osmmap_sub_menu_setting, false );
        lv_obj_move_foreground( osmmap_sub_menu_setting );
    }
    else {
        lv_obj_set_hidden( osmmap_sub_menu_setting, true );
        lv_obj_set_hidden( osmmap_sub_menu_layers, true );
    }
    osmmap_raise_primary_controls();
}

static void osmmap_tile_server_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED: {
            SpiRamJsonDocument doc( strlen( (const char*)osm_server_json_start ) * 2 );
            DeserializationError error = deserializeJson( doc, (const char *)osm_server_json_start );

            if ( error ) {
                OSMMAP_APP_ERROR_LOG("osm server list deserializeJson() failed: %s", error.c_str() );
            }
            else {
                if( doc.containsKey( lv_list_get_btn_text( obj ) ) ) {
                    const char *tile_server = doc[ lv_list_get_btn_text( obj ) ];
                    OSMMAP_APP_INFO_LOG("new tile server url: %s", tile_server );
                    strncpy( osmmap_config.osmmap, lv_list_get_btn_text( obj ), sizeof( osmmap_config.osmmap ) );
                    if ( osmmap_is_watch_flash_source_name( lv_list_get_btn_text( obj ) ) ) {
                        osmmap_configure_watch_flash_source(
                            osmmap_config.watch_flash_lon,
                            osmmap_config.watch_flash_lat,
                            osmmap_config.watch_flash_zoom,
                            false
                        );
                    }
                    else {
                        osmmap_watch_flash_mode = false;
                        osmmap_release_watch_flash_image( true );
                        osm_map_set_tile_server( osmmap_location, tile_server );
                        osmmap_apply_image_zoom();
                    }
                    osmmap_add_tile_server_list( osmmap_sub_menu_layers );
                    osmmap_update_request();
                }
            }
            doc.clear();
            lv_obj_set_hidden( osmmap_sub_menu_layers, true );            
            break;
        }
    }
}

void osmmap_add_tile_server_list( lv_obj_t *layers_list ) {
    lv_obj_t * list_btn;
    
    SpiRamJsonDocument doc( strlen( (const char*)osm_server_json_start ) * 2 );
    DeserializationError error = deserializeJson( doc, (const char *)osm_server_json_start );

    if ( error ) {
        OSMMAP_APP_ERROR_LOG("osm server list deserializeJson() failed: %s", error.c_str() );
    }
    else {
        while ( lv_list_remove( layers_list, 0 ) );
        JsonObject obj = doc.as<JsonObject>();
        for ( JsonPair p : obj ) {
            OSMMAP_APP_LOG("server: %s", p.key().c_str() );
            list_btn = lv_list_add_btn( layers_list, !strcmp( osmmap_config.osmmap, p.key().c_str() ) ? &checked_dark_16px : &unchecked_dark_16px, p.key().c_str() );
            lv_obj_set_event_cb( list_btn, osmmap_tile_server_event_cb );
            if ( !strcmp( osmmap_config.osmmap, p.key().c_str() ) ) {
                const char *osmmap_url = doc[ p.key().c_str() ];
                OSMMAP_APP_INFO_LOG("set osmmap url: %s, %s", p.key().c_str(), osmmap_url );
                if ( osmmap_is_watch_flash_source_name( p.key().c_str() ) ) {
                    osmmap_configure_watch_flash_source(
                        osmmap_config.watch_flash_lon,
                        osmmap_config.watch_flash_lat,
                        osmmap_config.watch_flash_zoom,
                        false
                    );
                }
                else {
                    osmmap_watch_flash_mode = false;
                    osmmap_release_watch_flash_image( true );
                    osm_map_set_tile_server( osmmap_location, osmmap_url );
                    osmmap_apply_image_zoom();
                }
            }
        }        
    }
    doc.clear();
}

void osmmap_activate_cb( void ) {
    /**
     * save block show messages state
     */
    osmmap_gps_state = gpsctl_get_autoon();
    if( osmmap_config.gps_autoon ) {
        osmmap_gps_enabled_seen = true;
        gpsctl_on();
    }
    else {
        osmmap_gps_enabled_seen = false;
        osmmap_gps_fix_seen = false;
        osmmap_have_local_position = false;
        osmmap_have_local_heading = false;
    }
    osmmap_update_gps_status_label();
    /**
     * save block show messages state
     */
    osmmap_wifi_state = wifictl_get_autoon();
    if( osmmap_config.wifi_autoon ) {
        wifictl_on();
        wifictl_set_autoon( osmmap_config.wifi_autoon );
    }
    /**
     * save block show messages state
     */
    osmmap_gps_on_standby_state = gpsctl_get_enable_on_standby();
    if ( osmmap_config.gps_on_standby ) {
        gpsctl_set_enable_on_standby( true );
    }
    /**
     * save block show messages state
     */
#ifdef NATIVE_64BIT

#else
    osmmap_block_watchface = watchface_get_enable_tile_after_wakeup();
    watchface_enable_tile_after_wakeup( false );
#endif
    /**
     * save block show messages state
     */
    osmmap_block_show_messages = blectl_get_show_notification();
    blectl_set_show_notification( false );
    /**
     * save black return to maintile state
     */
    osmmap_block_return_maintile = display_get_block_return_maintile();
    display_set_block_return_maintile( true );
    /**
     * force redraw screen
     */
    lv_obj_invalidate( lv_scr_act() );
    /**
     * set osm app active
     */
    osmmap_app_active = true;
    osmmap_reset_touch_pan();
    last_touch = millis();
    if ( osmmap_watch_flash_uses_current_tile() ) {
        osmmap_watch_flash_image_dirty = true;
        osmmap_load_watch_flash_image( true );
        osmmap_apply_image_zoom();
    }
#ifdef NATIVE_64BIT

#else
    /**
     * start background osm tile image update Task
     */
    xEventGroupClearBits( osmmap_event_handle, OSM_APP_TASK_EXIT_REQUEST );
    xTaskCreate(    osmmap_update_Task,      /* Function to implement the task */
                    "osmmap update Task",    /* Name of the task */
                    5000,                            /* Stack size in words */
                    NULL,                            /* Task input parameter */
                    1,                               /* Priority of the task */
                    &_osmmap_update_Task );  /* Task handle. */

    xTaskCreate(    osmmap_load_ahead_Task,      /* Function to implement the task */
                    "osmmap load ahead Task",    /* Name of the task */
                    5000,                            /* Stack size in words */
                    NULL,                            /* Task input parameter */
                    1,                               /* Priority of the task */
                    &_osmmap_load_ahead_Task );  /* Task handle. */
#endif
    osmmap_update_request();
    lv_img_cache_invalidate_src( osmmap_app_tile_img );
    powermgm_set_perf_mode();

    wf_image_button_fade_in( osmmap_exit_btn, 300, 0 );
    wf_image_button_fade_in( osmmap_zoom_in_btl, 300, 100 );
    wf_image_button_fade_in( osmmap_zoom_out_btl, 300, 200 );
    wf_image_button_fade_in( osmmap_layers_btn, 300, 300 );
}

void osmmap_hibernate_cb( void ) {
    /**
     * restore back to maintile and status force dark mode
     */
    blectl_set_show_notification( osmmap_block_show_messages );
    display_set_block_return_maintile( osmmap_block_return_maintile );
    gpsctl_set_autoon( osmmap_gps_state );
    wifictl_set_autoon( osmmap_wifi_state );
    gpsctl_set_enable_on_standby( osmmap_gps_on_standby_state );
#ifdef NATIVE_64BIT

#else
    watchface_enable_tile_after_wakeup( osmmap_block_watchface );
#endif
    /**
     * clear cache
     */
    osmmap_release_watch_flash_image( true );
    osm_map_clear_cache( osmmap_location );
    /**
     * set osm app inactive
     */
    osmmap_app_active = false;
    osmmap_reset_touch_pan();
    /**
     * stop background osm tile image update Task
     */
#ifdef NATIVE_64BIT
    eventmask |= OSM_APP_TASK_EXIT_REQUEST;
#else
    xEventGroupSetBits( osmmap_event_handle, OSM_APP_TASK_EXIT_REQUEST );
#endif
    powermgm_set_normal_mode();
    /**
     * save config
     */
    osmmap_config.save();
}

void osmmap_set_external_marker( double lon, double lat, const char *label ) {
    osmmap_external_marker_lon = lon;
    osmmap_external_marker_lat = lat;
    osmmap_external_marker_valid = true;

    if ( label ) {
        strncpy( osmmap_external_marker_label, label, sizeof( osmmap_external_marker_label ) - 1 );
        osmmap_external_marker_label[ sizeof( osmmap_external_marker_label ) - 1 ] = '\0';
    }
    else {
        osmmap_external_marker_label[ 0 ] = '\0';
    }

    if ( osmmap_location && !osmmap_have_local_position ) {
        osm_map_set_lon_lat( osmmap_location, lon, lat );
    }

    if ( osmmap_app_active ) {
        osmmap_update_request();
    }
}

void osmmap_clear_external_marker( void ) {
    osmmap_external_marker_valid = false;
    osmmap_external_marker_label[ 0 ] = '\0';

    if ( osmmap_app_active ) {
        osmmap_update_request();
    }
}

void osmmap_upsert_overlay_item( const char *key, const char *kind, double lon, double lat, const char *label, uint32_t updated_at, const char *color, bool has_pixel, int16_t pixel_x, int16_t pixel_y ) {
    osmmap_overlay_item_t *slot = NULL;
    uint32_t oldest_at = UINT32_MAX;
    size_t oldest_idx = 0;
    const osmmap_overlay_kind_t overlay_kind = osmmap_overlay_kind_from_name( kind );
    uint8_t color_r = 0;
    uint8_t color_g = 0;
    uint8_t color_b = 0;
    const bool has_color = osmmap_overlay_parse_color( color, &color_r, &color_g, &color_b );

    if ( overlay_kind == OSMMAP_OVERLAY_KIND_UNKNOWN ) {
        return;
    }
    if ( !key || !key[ 0 ] || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ) {
        return;
    }

    for ( size_t i = 0; i < OSMMAP_OVERLAY_MAX_ITEMS; i++ ) {
        if ( osmmap_overlay_items[ i ].used && !strcmp( osmmap_overlay_items[ i ].key, key ) ) {
            slot = &osmmap_overlay_items[ i ];
            break;
        }
        if ( !osmmap_overlay_items[ i ].used && !slot ) {
            slot = &osmmap_overlay_items[ i ];
        }
        if ( osmmap_overlay_items[ i ].used && osmmap_overlay_items[ i ].updated_at <= oldest_at ) {
            oldest_at = osmmap_overlay_items[ i ].updated_at;
            oldest_idx = i;
        }
    }

    if ( !slot ) {
        slot = &osmmap_overlay_items[ oldest_idx ];
        osmmap_hide_overlay_marker( slot );
    }

    slot->used = true;
    slot->kind = overlay_kind;
    slot->lon = lon;
    slot->lat = lat;
    slot->updated_at = updated_at ? updated_at : (uint32_t)millis();
    slot->has_pixel = has_pixel;
    slot->pixel_x = pixel_x;
    slot->pixel_y = pixel_y;
    slot->has_color = has_color;
    slot->color_r = color_r;
    slot->color_g = color_g;
    slot->color_b = color_b;
    strlcpy( slot->key, key, sizeof( slot->key ) );
    strlcpy( slot->label, label ? label : "", sizeof( slot->label ) );
    slot->replace_generation = osmmap_overlay_replace_active ? osmmap_overlay_replace_generation : 0;
    if ( slot->marker_obj ) {
        osmmap_ensure_overlay_marker( slot );
    }
    if ( osmmap_app_active ) {
        osmmap_refresh_marker_positions();
    }
}

void osmmap_begin_overlay_replace( void ) {
    osmmap_overlay_replace_generation++;
    if ( osmmap_overlay_replace_generation == 0 ) {
        osmmap_overlay_replace_generation++;
    }
    osmmap_overlay_replace_active = true;
}

void osmmap_commit_overlay_replace( void ) {
    if ( !osmmap_overlay_replace_active ) {
        return;
    }

    for ( size_t i = 0; i < OSMMAP_OVERLAY_MAX_ITEMS; i++ ) {
        if ( osmmap_overlay_items[ i ].used && osmmap_overlay_items[ i ].replace_generation != osmmap_overlay_replace_generation ) {
            osmmap_reset_overlay_item( &osmmap_overlay_items[ i ] );
        }
    }

    osmmap_overlay_replace_active = false;
    osmmap_refresh_marker_positions();
}

void osmmap_cancel_overlay_replace( void ) {
    osmmap_overlay_replace_active = false;
}

void osmmap_clear_overlay_items( void ) {
    osmmap_overlay_replace_active = false;
    for ( size_t i = 0; i < OSMMAP_OVERLAY_MAX_ITEMS; i++ ) {
        osmmap_reset_overlay_item( &osmmap_overlay_items[ i ] );
    }
    osmmap_refresh_marker_positions();
}

uint32_t osmmap_overlay_item_count( void ) {
    uint32_t count = 0;

    for ( size_t i = 0; i < OSMMAP_OVERLAY_MAX_ITEMS; i++ ) {
        if ( osmmap_overlay_items[ i ].used ) {
            count++;
        }
    }
    return( count );
}

bool osmmap_save_overlay_items( void ) {
#ifndef NATIVE_64BIT
    FILE *file = fopen( OSMMAP_OVERLAY_CACHE_PATH, "wb" );

    if ( !file ) {
        return( false );
    }

    for ( size_t i = 0; i < OSMMAP_OVERLAY_MAX_ITEMS; i++ ) {
        const osmmap_overlay_item_t *item = &osmmap_overlay_items[ i ];
        char color[ 8 ] = { 0 };
        char line[ 512 ] = { 0 };
        StaticJsonDocument< 512 > doc;

        if ( !item->used || item->kind < 0 || item->kind >= OSMMAP_OVERLAY_KIND_COUNT ) {
            continue;
        }

        doc[ "key" ] = item->key;
        doc[ "kind" ] = osmmap_overlay_kind_name( item->kind );
        doc[ "lon" ] = item->lon;
        doc[ "lat" ] = item->lat;
        doc[ "updatedAt" ] = item->updated_at;
        if ( item->label[ 0 ] ) {
            doc[ "label" ] = item->label;
        }
        if ( item->has_pixel ) {
            doc[ "mapX" ] = item->pixel_x;
            doc[ "mapY" ] = item->pixel_y;
        }
        if ( item->has_color ) {
            snprintf( color, sizeof( color ), "#%02x%02x%02x", item->color_r, item->color_g, item->color_b );
            doc[ "color" ] = color;
        }

        const size_t json_len = measureJson( doc );
        if ( json_len == 0 || json_len + 2 > sizeof( line ) ) {
            continue;
        }

        serializeJson( doc, line, sizeof( line ) );
        line[ json_len ] = '\n';
        if ( fwrite( line, 1, json_len + 1, file ) != json_len + 1 ) {
            fclose( file );
            return( false );
        }
    }

    fclose( file );
    return( true );
#else
    return( true );
#endif
}

bool osmmap_load_overlay_items( void ) {
#ifndef NATIVE_64BIT
    FILE *file = fopen( OSMMAP_OVERLAY_CACHE_PATH, "rb" );
    bool loaded = false;

    if ( !file ) {
        return( false );
    }

    char line[ 512 ];
    while ( fgets( line, sizeof( line ), file ) ) {
        StaticJsonDocument< 512 > doc;
        DeserializationError error = deserializeJson( doc, line );

        if ( error ) {
            continue;
        }

        const char *key = doc[ "key" ] | "";
        const char *kind = doc[ "kind" ] | "";
        const char *label = doc[ "label" ] | "";
        const char *color = doc[ "color" ] | "";
        const double lat = doc[ "lat" ].isNull() ? ( doc[ "latitude" ] | 999.0 ) : ( doc[ "lat" ] | 999.0 );
        const double lon = doc[ "lon" ].isNull() ? ( doc[ "longitude" ] | 999.0 ) : ( doc[ "lon" ] | 999.0 );
        const uint32_t updated_at = doc[ "updatedAt" ] | 0;
        const double map_x = doc[ "mapX" ].isNull() ? ( doc[ "px" ] | 999999.0 ) : ( doc[ "mapX" ] | 999999.0 );
        const double map_y = doc[ "mapY" ].isNull() ? ( doc[ "py" ] | 999999.0 ) : ( doc[ "mapY" ] | 999999.0 );
        const bool has_pixel = std::isfinite( map_x ) && std::isfinite( map_y ) && map_x >= 0.0 && map_x < 256.0 && map_y >= 0.0 && map_y < 256.0;
        const int16_t pixel_x = has_pixel ? (int16_t)lround( fmax( 0.0, fmin( 255.0, map_x ) ) ) : 0;
        const int16_t pixel_y = has_pixel ? (int16_t)lround( fmax( 0.0, fmin( 255.0, map_y ) ) ) : 0;

        if ( !key[ 0 ] || !kind[ 0 ] ) {
            continue;
        }
        if ( lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ) {
            continue;
        }

        osmmap_upsert_overlay_item( key, kind, lon, lat, label, updated_at, color, has_pixel, pixel_x, pixel_y );
        loaded = true;
    }

    fclose( file );
    if ( loaded ) {
        osmmap_refresh_marker_positions();
    }
    return( loaded );
#else
    return( false );
#endif
}

void osmmap_clear_persisted_overlay_items( void ) {
#ifndef NATIVE_64BIT
    remove( OSMMAP_OVERLAY_CACHE_PATH );
#endif
}

bool osmmap_apply_watch_basemap( const char *map_name, double lon, double lat, uint32_t zoom, uint32_t projection_zoom ) {
    const char *selected_name = ( map_name && map_name[ 0 ] ) ? map_name : OSMMAP_WATCH_MAP_NAME;
    bool found = false;

    SpiRamJsonDocument doc( strlen( (const char*)osm_server_json_start ) * 2 );
    DeserializationError error = deserializeJson( doc, (const char *)osm_server_json_start );

    if ( error ) {
        OSMMAP_APP_ERROR_LOG( "osm server list deserializeJson() failed: %s", error.c_str() );
        return( false );
    }

    JsonObject obj = doc.as<JsonObject>();
    for ( JsonPair p : obj ) {
        if ( !strcmp( p.key().c_str(), selected_name ) ) {
            found = true;
            break;
        }
    }

    if ( !found ) {
        OSMMAP_APP_ERROR_LOG( "watch basemap source not found: %s", selected_name );
        return( false );
    }

    strncpy( osmmap_config.osmmap, selected_name, sizeof( osmmap_config.osmmap ) );
    if ( !osmmap_configure_watch_flash_source( lon, lat, zoom, true, projection_zoom ) ) {
        return( false );
    }

    if ( osmmap_app_active ) {
        osmmap_reset_active_tile_image();
        osmmap_update_request();
    }

    return( true );
}

bool osmmap_get_watch_basemap_info( char *path, size_t path_size, size_t *bytes ) {
    const char *file_path = NULL;

    if ( path && path_size ) {
        path[ 0 ] = '\0';
    }
    if ( bytes ) {
        *bytes = 0;
    }

    if ( strncmp( osmmap_watch_flash_uri, "file://", 7 ) == 0 ) {
        file_path = osmmap_watch_flash_uri + 7;
    }
    else if ( osmmap_watch_flash_uri[ 0 ] ) {
        file_path = osmmap_watch_flash_uri;
    }

    if ( !file_path || !file_path[ 0 ] ) {
        return( false );
    }
    if ( path && path_size ) {
        strlcpy( path, file_path, path_size );
    }

#ifndef NATIVE_64BIT
    struct stat st;

    if ( stat( file_path, &st ) == 0 ) {
        if ( bytes ) {
            *bytes = (size_t)st.st_size;
        }
        return( true );
    }
    return( false );
#else
    return( true );
#endif
}

void osmmap_prepare_watch_basemap_file_replace( const char *path ) {
    if ( !path || strcmp( path, OSMMAP_WATCH_CURRENT_TILE_PATH ) != 0 ) {
        return;
    }

    osmmap_watch_flash_image_dirty = true;
    if ( osmmap_watch_flash_image_ready || osmmap_watch_flash_uses_current_tile() ) {
        osmmap_release_watch_flash_image( true );
    }
    if ( osmmap_app_active && osmmap_watch_flash_uses_current_tile() ) {
        osmmap_update_request();
    }
}
