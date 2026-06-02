#include "config.h"
#include "xnode_sos.h"

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

LV_IMG_DECLARE( notification_64px );

namespace {
    icon_t *xnode_sos_app = NULL;
    lv_obj_t *xnode_sos_tile = NULL;
    lv_obj_t *xnode_sos_status_label = NULL;
    uint32_t xnode_sos_tile_num = 0;

    void xnode_sos_set_status( const char *text ) {
        if ( xnode_sos_status_label ) {
            lv_label_set_text( xnode_sos_status_label, text ? text : "" );
        }
    }

    void xnode_sos_open_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                xnode_sos_set_status( "Ready" );
                mainbar_jump_to_tilenumber( xnode_sos_tile_num, LV_ANIM_OFF, true );
                break;
            default:
                break;
        }
    }

    void xnode_sos_exit_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                mainbar_jump_back();
                break;
            default:
                break;
        }
    }

    void xnode_sos_send_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                xnode_sos_set_status( xnode_send_manual_sos() ? "SOS sent over mesh" : "SOS not sent" );
                break;
            default:
                break;
        }
    }

    bool xnode_sos_button_event_cb( EventBits_t event, void *arg ) {
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

void xnode_sos_tile_setup( void ) {
    const lv_coord_t content_width = lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 2 );

    xnode_sos_tile_num = mainbar_add_app_tile( 1, 1, "Manual SOS" );
    xnode_sos_tile = mainbar_get_tile_obj( xnode_sos_tile_num );
    lv_obj_add_style( xnode_sos_tile, LV_OBJ_PART_MAIN, ws_get_app_opa_style() );

    lv_obj_t *exit_btn = wf_add_close_button( xnode_sos_tile, xnode_sos_exit_event_cb );
    lv_obj_align( exit_btn, xnode_sos_tile, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, THEME_PADDING );

    lv_obj_t *title = lv_label_create( xnode_sos_tile, NULL );
    lv_obj_add_style( title, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_label_set_text( title, "Manual SOS" );
    lv_obj_align( title, xnode_sos_tile, LV_ALIGN_IN_TOP_LEFT, THEME_PADDING, THEME_PADDING );

    lv_obj_t *summary = lv_label_create( xnode_sos_tile, NULL );
    lv_obj_add_style( summary, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_label_set_long_mode( summary, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( summary, content_width );
    lv_label_set_text( summary, "P1 HELP SITREP\nManual SOS" );
    lv_obj_set_width( summary, content_width );
    lv_obj_align( summary, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING );

    lv_obj_t *send_btn = lv_btn_create( xnode_sos_tile, NULL );
    lv_obj_add_style( send_btn, LV_BTN_PART_MAIN, ws_get_button_style() );
    lv_obj_set_size( send_btn, content_width, 56 );
    lv_obj_align( send_btn, summary, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING * 2 );
    lv_obj_set_event_cb( send_btn, xnode_sos_send_event_cb );

    lv_obj_t *send_label = lv_label_create( send_btn, NULL );
    lv_label_set_text( send_label, "SEND SOS" );
    lv_obj_align( send_label, send_btn, LV_ALIGN_CENTER, 0, 0 );

    xnode_sos_status_label = lv_label_create( xnode_sos_tile, NULL );
    lv_obj_add_style( xnode_sos_status_label, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_label_set_long_mode( xnode_sos_status_label, LV_LABEL_LONG_DOT );
    lv_obj_set_width( xnode_sos_status_label, content_width );
    lv_label_set_text( xnode_sos_status_label, "Ready" );
    lv_obj_set_width( xnode_sos_status_label, content_width );
    lv_obj_align( xnode_sos_status_label, send_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING );

    mainbar_add_tile_button_cb( xnode_sos_tile_num, xnode_sos_button_event_cb );
    xnode_sos_app = app_register( "SOS", &notification_64px, xnode_sos_open_event_cb );
}
