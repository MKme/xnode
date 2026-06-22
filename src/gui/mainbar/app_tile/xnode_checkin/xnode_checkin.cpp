#include "config.h"
#include "xnode_checkin.h"

#include "gui/app.h"
#include "gui/mainbar/mainbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/ble/xnode.h"
#include "hardware/button.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

LV_IMG_DECLARE( location_64px );

namespace {
    icon_t *xnode_checkin_app = NULL;
    lv_obj_t *xnode_checkin_tile = NULL;
    lv_obj_t *xnode_checkin_status_label = NULL;
    uint32_t xnode_checkin_tile_num = 0;

    void xnode_checkin_set_status( const char *text ) {
        if ( xnode_checkin_status_label ) {
            lv_label_set_text( xnode_checkin_status_label, text ? text : "" );
        }
    }

    void xnode_checkin_open_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                xnode_checkin_set_status( "Ready" );
                mainbar_jump_to_tilenumber( xnode_checkin_tile_num, LV_ANIM_OFF, true );
                break;
            default:
                break;
        }
    }

    void xnode_checkin_exit_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                mainbar_jump_back();
                break;
            default:
                break;
        }
    }

    void xnode_checkin_send_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                xnode_checkin_set_status( xnode_send_manual_checkin() ? "Check-in sent over mesh" : "Check-in not sent" );
                break;
            default:
                break;
        }
    }

    bool xnode_checkin_button_event_cb( EventBits_t event, void *arg ) {
        switch( event ) {
            case BUTTON_EXIT:
                mainbar_jump_back();
                break;
            default:
                break;
        }
        return( true );
    }
}

void xnode_checkin_tile_setup( void ) {
    const lv_coord_t content_width = lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 2 );

    xnode_checkin_tile_num = mainbar_add_app_tile( 1, 1, "CheckIn" );
    xnode_checkin_tile = mainbar_get_tile_obj( xnode_checkin_tile_num );
    lv_obj_add_style( xnode_checkin_tile, LV_OBJ_PART_MAIN, ws_get_app_opa_style() );

    #if defined( LILYGO_WATCH_ULTRA )
        lv_obj_t *exit_btn = wf_add_exit_button( xnode_checkin_tile, xnode_checkin_exit_event_cb );
        lv_obj_align( exit_btn, xnode_checkin_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_PADDING, -THEME_PADDING );
    #else
        lv_obj_t *exit_btn = wf_add_close_button( xnode_checkin_tile, xnode_checkin_exit_event_cb );
        lv_obj_align( exit_btn, xnode_checkin_tile, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, THEME_PADDING );
    #endif

    lv_obj_t *title = lv_label_create( xnode_checkin_tile, NULL );
    lv_obj_add_style( title, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_label_set_text( title, "CheckIn" );
    lv_obj_align( title, xnode_checkin_tile, LV_ALIGN_IN_TOP_LEFT, THEME_PADDING, THEME_PADDING );

    lv_obj_t *summary = lv_label_create( xnode_checkin_tile, NULL );
    lv_obj_add_style( summary, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_label_set_long_mode( summary, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( summary, content_width );
    lv_label_set_text( summary, "OK CHECKIN/LOC\ncurrent position" );
    lv_obj_set_width( summary, content_width );
    lv_obj_align( summary, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING );

    lv_obj_t *send_btn = lv_btn_create( xnode_checkin_tile, NULL );
    lv_obj_add_style( send_btn, LV_BTN_PART_MAIN, ws_get_button_style() );
    lv_obj_set_size( send_btn, content_width, 56 );
    lv_obj_align( send_btn, summary, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING * 2 );
    lv_obj_set_event_cb( send_btn, xnode_checkin_send_event_cb );

    lv_obj_t *send_label = lv_label_create( send_btn, NULL );
    lv_label_set_text( send_label, "CHECK IN" );
    lv_obj_align( send_label, send_btn, LV_ALIGN_CENTER, 0, 0 );

    xnode_checkin_status_label = lv_label_create( xnode_checkin_tile, NULL );
    lv_obj_add_style( xnode_checkin_status_label, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_label_set_long_mode( xnode_checkin_status_label, LV_LABEL_LONG_DOT );
    lv_obj_set_width( xnode_checkin_status_label, content_width );
    lv_label_set_text( xnode_checkin_status_label, "Ready" );
    lv_obj_set_width( xnode_checkin_status_label, content_width );
    lv_obj_align( xnode_checkin_status_label, send_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING );

    mainbar_add_tile_button_cb( xnode_checkin_tile_num, xnode_checkin_button_event_cb );
    xnode_checkin_app = app_register( "CheckIn", &location_64px, xnode_checkin_open_event_cb );
}
