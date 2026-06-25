/****************************************************************************
 *   Aug 11 17:13:51 2020
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

#include "compass_app.h"
#include "compass_app_main.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/app.h"
#include "gui/widget_styles.h"
#include "gui/widget_factory.h"

#include "hardware/compass.h"
#include "hardware/display.h"
#include "hardware/gpsctl.h"
#include "utils/alloc.h"

#include <math.h>

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include "utils/millis.h"
#else
    #include <Arduino.h>
#endif
/**
 * local lv obj 
 */
lv_obj_t *compass_exit_btn = NULL;
lv_obj_t *compass_rose_canvas = NULL;
lv_obj_t *compass_heading_label = NULL;

static const uint16_t COMPASS_ROSE_SIZE = 160;
static lv_color_t *compass_rose_canvas_buf = NULL;
static bool compass_page_active = false;
static bool compass_heading_valid = false;
static double compass_current_heading = 0.0;
static bool compass_display_timeout_saved = false;
static uint32_t compass_display_timeout = DISPLAY_MIN_TIMEOUT;
/**
 * call back functions
 */
static bool compass_app_main_update_event_cb( EventBits_t event, void *arg );
static bool compass_app_main_gps_event_cb( EventBits_t event, void *arg );
static void compass_app_main_activate_cb( void );
static void compass_app_main_hibernate_cb( void );
static bool compass_app_main_button_cb( EventBits_t event, void *arg );
/*
 *
 */
LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_32px);

static double compass_app_main_normalize_heading( double heading ) {
    if ( heading > 360.0 ) {
        heading /= 100.0;
    }

    while ( heading < 0.0 ) {
        heading += 360.0;
    }
    while ( heading >= 360.0 ) {
        heading -= 360.0;
    }

    return( heading );
}

static const char *compass_app_main_direction( double heading ) {
    static const char *directions[] = {
        "N", "NNE", "NE", "ENE",
        "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW",
        "W", "WNW", "NW", "NNW"
    };
    int index = (int)( ( compass_app_main_normalize_heading( heading ) + 11.25 ) / 22.5 );
    return( directions[ index & 0x0f ] );
}

static lv_point_t compass_app_main_point( double angle_deg, double radius ) {
    const double radians = angle_deg * 0.01745329251994329577;
    const double center = ( (double)COMPASS_ROSE_SIZE - 1.0 ) * 0.5;
    lv_point_t point = {
        (lv_coord_t)( center + sin( radians ) * radius + 0.5 ),
        (lv_coord_t)( center - cos( radians ) * radius + 0.5 )
    };
    return( point );
}

static void compass_app_main_draw_tick( double base_angle, double heading, bool major, bool cardinal ) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init( &line_dsc );
    line_dsc.color = cardinal ? LV_COLOR_MAKE( 255, 92, 80 ) : ( major ? LV_COLOR_WHITE : LV_COLOR_MAKE( 150, 170, 190 ) );
    line_dsc.width = cardinal ? 4 : ( major ? 3 : 2 );
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    const double outer_radius = ( (double)COMPASS_ROSE_SIZE * 0.5 ) - 6.0;
    const double tick_length = cardinal ? 18.0 : ( major ? 14.0 : 9.0 );
    const double angle = base_angle - heading;
    lv_point_t points[2] = {
        compass_app_main_point( angle, outer_radius - tick_length ),
        compass_app_main_point( angle, outer_radius )
    };
    lv_canvas_draw_line( compass_rose_canvas, points, 2, &line_dsc );
}

static void compass_app_main_draw_cardinal( const char *text, double base_angle, double heading ) {
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init( &label_dsc );
    label_dsc.color = text[0] == 'N' ? LV_COLOR_MAKE( 255, 92, 80 ) : LV_COLOR_WHITE;
    label_dsc.font = &Ubuntu_16px;

    const double angle = base_angle - heading;
    lv_point_t point = compass_app_main_point( angle, ( (double)COMPASS_ROSE_SIZE * 0.5 ) - 32.0 );
    lv_canvas_draw_text( compass_rose_canvas, point.x - 15, point.y - 10, 30, &label_dsc, text, LV_LABEL_ALIGN_CENTER );
}

static void compass_app_main_draw_fixed_pointer( void ) {
    lv_draw_line_dsc_t pointer_dsc;
    lv_draw_line_dsc_init( &pointer_dsc );
    pointer_dsc.color = LV_COLOR_MAKE( 255, 196, 60 );
    pointer_dsc.width = 4;
    pointer_dsc.opa = LV_OPA_COVER;
    pointer_dsc.round_start = 1;
    pointer_dsc.round_end = 1;

    const lv_coord_t center = COMPASS_ROSE_SIZE / 2;
    lv_point_t pointer[3] = {
        { (lv_coord_t)( center - 7 ), 16 },
        { center, 4 },
        { (lv_coord_t)( center + 7 ), 16 }
    };
    lv_canvas_draw_line( compass_rose_canvas, pointer, 3, &pointer_dsc );
}

static void compass_app_main_draw_rose( double heading, bool valid ) {
    if ( !compass_rose_canvas || !compass_rose_canvas_buf ) {
        return;
    }

    lv_canvas_fill_bg( compass_rose_canvas, LV_COLOR_TRANSP, LV_OPA_COVER );

    lv_draw_line_dsc_t circle_dsc;
    lv_draw_line_dsc_init( &circle_dsc );
    circle_dsc.color = LV_COLOR_MAKE( 74, 98, 126 );
    circle_dsc.width = 2;
    circle_dsc.opa = LV_OPA_COVER;
    lv_canvas_draw_arc(
        compass_rose_canvas,
        COMPASS_ROSE_SIZE / 2,
        COMPASS_ROSE_SIZE / 2,
        ( COMPASS_ROSE_SIZE / 2 ) - 7,
        0,
        360,
        &circle_dsc
    );

    const double rose_heading = valid ? compass_app_main_normalize_heading( heading ) : 0.0;
    for ( int tick = 0; tick < 16; tick++ ) {
        const bool cardinal = ( tick % 4 ) == 0;
        const bool major = ( tick % 2 ) == 0;
        compass_app_main_draw_tick( tick * 22.5, rose_heading, major, cardinal );
    }

    compass_app_main_draw_cardinal( "N", 0.0, rose_heading );
    compass_app_main_draw_cardinal( "E", 90.0, rose_heading );
    compass_app_main_draw_cardinal( "S", 180.0, rose_heading );
    compass_app_main_draw_cardinal( "W", 270.0, rose_heading );
    compass_app_main_draw_fixed_pointer();
}

static void compass_app_main_set_heading( double heading, const char *source ) {
    compass_current_heading = compass_app_main_normalize_heading( heading );
    compass_heading_valid = true;

    if ( compass_heading_label ) {
        char heading_text[48];
        snprintf(
            heading_text,
            sizeof( heading_text ),
            "%s %03d deg %s",
            source,
            (int)( compass_current_heading + 0.5 ) % 360,
            compass_app_main_direction( compass_current_heading )
        );
        lv_label_set_text( compass_heading_label, heading_text );
    }

    if ( compass_page_active ) {
        compass_app_main_draw_rose( compass_current_heading, true );
    }
}

static void compass_app_main_set_waiting_text( void ) {
    if ( !compass_heading_label ) {
        return;
    }

    if ( compass_available() ) {
        lv_label_set_text( compass_heading_label, "Compass starting" );
    }
    else {
        lv_label_set_text( compass_heading_label, "GPS course needs movement" );
    }
}

/*
 * setup routine for wifimon app
 */
void compass_app_main_setup( uint32_t tile ) {
    /**
     * add exit, menu and setup button to the main app tile
     */
    compass_exit_btn = wf_add_exit_button( mainbar_get_tile_obj( tile ) );
    lv_obj_align( compass_exit_btn, mainbar_get_tile_obj( tile ), LV_ALIGN_IN_BOTTOM_LEFT, THEME_PADDING, -THEME_PADDING );

    compass_heading_label = wf_add_label( mainbar_get_tile_obj( tile ), "", APP_ICON_LABEL_STYLE );
    lv_obj_set_style_local_text_color( compass_heading_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
    lv_obj_align( compass_heading_label, mainbar_get_tile_obj( tile ), LV_ALIGN_IN_TOP_MID, 0, THEME_PADDING );

    compass_rose_canvas = lv_canvas_create( mainbar_get_tile_obj( tile ), NULL );
    if ( !compass_rose_canvas_buf ) {
        compass_rose_canvas_buf = (lv_color_t*)MALLOC( sizeof( lv_color_t ) * COMPASS_ROSE_SIZE * COMPASS_ROSE_SIZE );
    }
    if ( compass_rose_canvas_buf ) {
        lv_canvas_set_buffer(
            compass_rose_canvas,
            compass_rose_canvas_buf,
            COMPASS_ROSE_SIZE,
            COMPASS_ROSE_SIZE,
            LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED
        );
    }
    else if ( compass_heading_label ) {
        lv_label_set_text( compass_heading_label, "Compass display unavailable" );
    }
    lv_obj_align( compass_rose_canvas, mainbar_get_tile_obj( tile ), LV_ALIGN_CENTER, 0, 4 );

    compass_register_cb( COMPASS_UPDATE, compass_app_main_update_event_cb, "compass updates");
    gpsctl_register_cb( GPSCTL_UPDATE_COURSE, compass_app_main_gps_event_cb, "compass gps course" );
    mainbar_add_tile_activate_cb( tile, compass_app_main_activate_cb );
    mainbar_add_tile_hibernate_cb( tile, compass_app_main_hibernate_cb );
    mainbar_add_tile_button_cb( tile, compass_app_main_button_cb );

    compass_app_main_set_waiting_text();
    compass_app_main_draw_rose( 0.0, false );
}

static bool compass_app_main_update_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case COMPASS_UPDATE: {
            compass_data_t *compass_data = (compass_data_t*)arg;
            if ( compass_data ) {
                compass_app_main_set_heading( compass_data->azimuth, "MAG" );
            }
            break;
        }
    }
    return( true );
}

static bool compass_app_main_gps_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GPSCTL_UPDATE_COURSE: {
            gps_data_t *gps_data = (gps_data_t*)arg;
            if ( !compass_available() && gps_data && gps_data->valid_course ) {
                compass_app_main_set_heading( gps_data->course, "GPS" );
            }
            break;
        }
    }
    return( true );
}

/**
 * @brief call back function for button if the current tile active
 * 
 * @param event         event like BUTTON_LEFT, BUTTON_RIGHT, ...
 * @param arg           here like NULL
 * @return true 
 * @return false 
 */
static bool compass_app_main_button_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case BUTTON_EXIT:
            mainbar_jump_back();
            break;
    }
    return( true );
}
/**
 * @brief call back function if the current tile activate
 * 
 */
static void compass_app_main_activate_cb( void ) {
    if ( !compass_display_timeout_saved ) {
        compass_display_timeout = display_get_timeout();
        compass_display_timeout_saved = true;
    }
    display_set_timeout( DISPLAY_NO_TIMEOUT );
    display_trigger_activity();

    compass_page_active = true;
    if ( compass_available() ) {
        compass_on();
        compass_start_calibration();
    }
    if ( compass_heading_valid ) {
        compass_app_main_draw_rose( compass_current_heading, true );
    }
    else {
        compass_app_main_set_waiting_text();
        compass_app_main_draw_rose( 0.0, false );
    }
}
/**
 * @brief call back function if the current tile hibernate
 * 
 */
static void compass_app_main_hibernate_cb( void ) {
    compass_page_active = false;
    if ( compass_available() ) {
        compass_off();
    }
    if ( compass_display_timeout_saved ) {
        display_set_timeout( compass_display_timeout );
        compass_display_timeout_saved = false;
    }
}
