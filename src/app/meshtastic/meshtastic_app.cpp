#include "config.h"
#include "meshtastic_app.h"
#include "meshtastic_service.h"

#include "gui/app.h"
#include "gui/keyboard.h"
#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/powermgm.h"

#include <inttypes.h>

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

uint32_t meshtastic_app_tile_num;
icon_t *meshtastic_app = NULL;

static lv_obj_t *meshtastic_app_tile = NULL;
static lv_obj_t *meshtastic_node_label = NULL;
static lv_obj_t *meshtastic_status_label = NULL;
static lv_obj_t *meshtastic_last_label = NULL;
static lv_obj_t *meshtastic_input = NULL;
static lv_obj_t *meshtastic_send_btn = NULL;
static lv_obj_t *meshtastic_inbox_btn = NULL;
static lv_obj_t *meshtastic_exit_btn = NULL;

LV_IMG_DECLARE(message_64px);

static int registed = app_autocall_function( &meshtastic_app_setup, 16 );

static void meshtastic_app_refresh( void );
static void enter_meshtastic_app_event_cb( lv_obj_t * obj, lv_event_t event );
static void exit_meshtastic_app_event_cb( lv_obj_t * obj, lv_event_t event );
static void meshtastic_input_event_cb( lv_obj_t * obj, lv_event_t event );
static void meshtastic_send_event_cb( lv_obj_t * obj, lv_event_t event );
static void meshtastic_inbox_event_cb( lv_obj_t * obj, lv_event_t event );
static bool meshtastic_app_loop_cb( EventBits_t event, void *arg );

void meshtastic_app_setup( void ) {
    if ( !registed ) {
        return;
    }

    meshtastic_app_tile_num = mainbar_add_app_tile( 1, 1, "meshtastic app" );
    meshtastic_app_tile = mainbar_get_tile_obj( meshtastic_app_tile_num );
    lv_obj_add_style( meshtastic_app_tile, LV_OBJ_PART_MAIN, ws_get_app_opa_style() );

    meshtastic_app = app_register( "mesh", &message_64px, enter_meshtastic_app_event_cb );

    meshtastic_exit_btn = wf_add_close_button( meshtastic_app_tile, exit_meshtastic_app_event_cb );
    lv_obj_align( meshtastic_exit_btn, meshtastic_app_tile, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, THEME_PADDING );

    meshtastic_node_label = lv_label_create( meshtastic_app_tile, NULL );
    lv_obj_add_style( meshtastic_node_label, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_obj_set_width( meshtastic_node_label, lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 3 ) - 24 );
    lv_label_set_long_mode( meshtastic_node_label, LV_LABEL_LONG_DOT );
    lv_label_set_text( meshtastic_node_label, "" );
    lv_obj_align( meshtastic_node_label, meshtastic_app_tile, LV_ALIGN_IN_TOP_LEFT, THEME_PADDING, THEME_PADDING );

    meshtastic_status_label = lv_label_create( meshtastic_app_tile, NULL );
    lv_obj_add_style( meshtastic_status_label, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_obj_set_width( meshtastic_status_label, lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 3 ) - 24 );
    lv_label_set_long_mode( meshtastic_status_label, LV_LABEL_LONG_DOT );
    lv_label_set_text( meshtastic_status_label, "" );
    lv_obj_align( meshtastic_status_label, meshtastic_node_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING / 2 );

    meshtastic_last_label = lv_label_create( meshtastic_app_tile, NULL );
    lv_obj_add_style( meshtastic_last_label, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_obj_set_width( meshtastic_last_label, lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 2 ) );
    lv_label_set_long_mode( meshtastic_last_label, LV_LABEL_LONG_DOT );
    lv_label_set_text( meshtastic_last_label, "" );
    lv_obj_align( meshtastic_last_label, meshtastic_status_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING / 2 );

    meshtastic_input = lv_textarea_create( meshtastic_app_tile, NULL );
    lv_obj_set_width( meshtastic_input, lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 2 ) );
    lv_obj_set_height( meshtastic_input, 72 );
    lv_textarea_set_text( meshtastic_input, "" );
    lv_textarea_set_pwd_mode( meshtastic_input, false );
    lv_textarea_set_one_line( meshtastic_input, false );
    lv_textarea_set_cursor_hidden( meshtastic_input, false );
    lv_textarea_set_max_length( meshtastic_input, 80 );
    lv_textarea_set_placeholder_text( meshtastic_input, "Compose mesh message" );
    lv_obj_add_style( meshtastic_input, LV_OBJ_PART_MAIN, ws_get_button_style() );
    lv_obj_align( meshtastic_input, meshtastic_last_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING );
    lv_obj_set_event_cb( meshtastic_input, meshtastic_input_event_cb );

    meshtastic_send_btn = lv_btn_create( meshtastic_app_tile, NULL );
    lv_obj_add_style( meshtastic_send_btn, LV_BTN_PART_MAIN, ws_get_button_style() );
    lv_obj_set_size( meshtastic_send_btn, ( lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 3 ) ) / 2, 36 );
    lv_obj_align( meshtastic_send_btn, meshtastic_input, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING );
    lv_obj_set_event_cb( meshtastic_send_btn, meshtastic_send_event_cb );
    lv_obj_t *send_label = lv_label_create( meshtastic_send_btn, NULL );
    lv_label_set_text( send_label, "send" );
    lv_obj_align( send_label, meshtastic_send_btn, LV_ALIGN_CENTER, 0, 0 );

    meshtastic_inbox_btn = lv_btn_create( meshtastic_app_tile, NULL );
    lv_obj_add_style( meshtastic_inbox_btn, LV_BTN_PART_MAIN, ws_get_button_style() );
    lv_obj_set_size( meshtastic_inbox_btn, ( lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 3 ) ) / 2, 36 );
    lv_obj_align( meshtastic_inbox_btn, meshtastic_send_btn, LV_ALIGN_OUT_RIGHT_MID, THEME_PADDING, 0 );
    lv_obj_set_event_cb( meshtastic_inbox_btn, meshtastic_inbox_event_cb );
    lv_obj_t *inbox_label = lv_label_create( meshtastic_inbox_btn, NULL );
    lv_label_set_text( inbox_label, "inbox" );
    lv_obj_align( inbox_label, meshtastic_inbox_btn, LV_ALIGN_CENTER, 0, 0 );

    meshtastic_service_setup();
    meshtastic_app_refresh();

    powermgm_register_loop_cb( POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP, meshtastic_app_loop_cb, "meshtastic app loop" );
}

static void meshtastic_app_refresh( void ) {
    char node[ 32 ];
    char status[ 96 ];
    char link[ 96 ];
    const lv_coord_t summary_width = lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 3 ) - 24;
    const lv_coord_t link_width = lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 2 );

    if ( meshtastic_service_get_last_peer() != 0 ) {
        snprintf(
            link,
            sizeof( link ),
            "last !%08" PRIX32 "  %ddBm  %.1fdB",
            meshtastic_service_get_last_peer(),
            meshtastic_service_get_last_rssi(),
            meshtastic_service_get_last_snr()
        );
    }
    else {
        snprintf( link, sizeof( link ), "LongFast  US 906.875MHz" );
    }

    snprintf( node, sizeof( node ), "me !%08" PRIX32, meshtastic_service_get_node_id() );
    snprintf( status, sizeof( status ), "%s", meshtastic_service_get_status() );

    lv_label_set_text( meshtastic_node_label, node );
    lv_obj_set_width( meshtastic_node_label, summary_width );
    lv_label_set_text( meshtastic_status_label, status );
    lv_obj_set_width( meshtastic_status_label, summary_width );
    lv_label_set_text( meshtastic_last_label, link );
    lv_obj_set_width( meshtastic_last_label, link_width );
}

static void enter_meshtastic_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED:
            meshtastic_app_refresh();
            mainbar_jump_to_tilenumber( meshtastic_app_tile_num, LV_ANIM_OFF, true );
            app_hide_indicator( meshtastic_app );
            break;
    }
}

static void exit_meshtastic_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED:
            mainbar_jump_back();
            break;
    }
}

static void meshtastic_input_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED:
            keyboard_set_textarea( obj );
            break;
    }
}

static void meshtastic_send_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED:
            if ( meshtastic_service_send_text( lv_textarea_get_text( meshtastic_input ) ) ) {
                lv_textarea_set_text( meshtastic_input, "" );
            }
            meshtastic_app_refresh();
            break;
    }
}

static void meshtastic_inbox_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED:
            bluetooth_message_open();
            break;
    }
}

static bool meshtastic_app_loop_cb( EventBits_t event, void *arg ) {
    static uint32_t next_update = 0;

    if ( millis() < next_update ) {
        return( true );
    }

    next_update = millis() + 1000;
    meshtastic_app_refresh();
    return( true );
}
