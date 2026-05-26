#include "config.h"
#include "xnode_notifications.h"
#include "config/xnode_notifications_config.h"

#include <ArduinoJson.h>
#include <time.h>

#include "gui/app.h"
#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "hardware/button.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

LV_IMG_DECLARE( notification_64px );

namespace {
    xnode_notifications_config_t *xnode_notifications_config = NULL;
    xnode_notification_entry_t xnode_notification_history[ XNODE_NOTIFICATIONS_HISTORY_MAX ];
    uint32_t xnode_notification_count = 0;
    uint32_t xnode_notification_sequence = 0;

    icon_t *xnode_notifications_app = NULL;
    lv_obj_t *xnode_notifications_tile = NULL;
    lv_obj_t *xnode_notifications_enabled_switch = NULL;
    lv_obj_t *xnode_notifications_page = NULL;
    lv_obj_t *xnode_notifications_label = NULL;
    uint32_t xnode_notifications_tile_num = 0;

    void xnode_notifications_load_history_from_config( void ) {
        if ( !xnode_notifications_config ) {
            return;
        }

        const uint32_t limit = xnode_notifications_config->history_limit;
        xnode_notification_count = xnode_notifications_config->history_count;
        if ( xnode_notification_count > limit ) {
            xnode_notification_count = limit;
        }
        if ( xnode_notification_count > XNODE_NOTIFICATIONS_HISTORY_MAX ) {
            xnode_notification_count = XNODE_NOTIFICATIONS_HISTORY_MAX;
        }

        xnode_notification_sequence = 0;
        for ( uint32_t i = 0; i < xnode_notification_count; i++ ) {
            xnode_notification_history[ i ] = xnode_notifications_config->history[ i ];
            if ( xnode_notification_history[ i ].id > xnode_notification_sequence ) {
                xnode_notification_sequence = xnode_notification_history[ i ].id;
            }
        }
    }

    void xnode_notifications_save_history_to_config( void ) {
        if ( !xnode_notifications_config ) {
            return;
        }

        xnode_notifications_config->history_count = xnode_notification_count > XNODE_NOTIFICATIONS_HISTORY_MAX ? XNODE_NOTIFICATIONS_HISTORY_MAX : xnode_notification_count;
        for ( uint32_t i = 0; i < xnode_notifications_config->history_count; i++ ) {
            xnode_notifications_config->history[ i ] = xnode_notification_history[ i ];
        }
        xnode_notifications_config->save();
    }

    void xnode_notifications_ensure_config( void ) {
        if ( xnode_notifications_config ) {
            return;
        }
        xnode_notifications_config = new xnode_notifications_config_t();
        xnode_notifications_config->load();
        xnode_notifications_load_history_from_config();
    }

    uint32_t xnode_notifications_history_limit( void ) {
        xnode_notifications_ensure_config();
        uint32_t limit = xnode_notifications_config ? xnode_notifications_config->history_limit : XNODE_NOTIFICATIONS_HISTORY_MAX;
        if ( limit < 5 ) {
            limit = 5;
        }
        if ( limit > XNODE_NOTIFICATIONS_HISTORY_MAX ) {
            limit = XNODE_NOTIFICATIONS_HISTORY_MAX;
        }
        return( limit );
    }

    void xnode_notifications_copy_field( char *out, size_t out_size, const char *value, const char *fallback ) {
        const char *src = ( value && value[ 0 ] ) ? value : fallback;
        if ( !out || out_size == 0 ) {
            return;
        }
        strlcpy( out, src ? src : "", out_size );
    }

    void xnode_notifications_format_time( uint32_t ts, char *out, size_t out_size ) {
        if ( !out || out_size == 0 ) {
            return;
        }
        if ( ts < 946684800UL ) {
            strlcpy( out, "--:--", out_size );
            return;
        }

        time_t raw = (time_t)ts;
        struct tm info;
        localtime_r( &raw, &info );
        snprintf( out, out_size, "%02d:%02d", info.tm_hour, info.tm_min );
    }

    void xnode_notifications_render( void ) {
        if ( !xnode_notifications_label ) {
            return;
        }

        char text[ 4096 ];
        size_t used = 0;
        const uint32_t count = xnode_notification_count;

        if ( count == 0 ) {
            lv_label_set_text( xnode_notifications_label, "No XNODE news alerts yet." );
            return;
        }

        text[ 0 ] = '\0';
        for ( uint32_t i = 0; i < count && used < sizeof( text ) - 1; i++ ) {
            char when[ 16 ];
            char line[ 320 ];
            xnode_notifications_format_time( xnode_notification_history[ i ].ts, when, sizeof( when ) );
            snprintf(
                line,
                sizeof( line ),
                "%s  %s\n%s\n%s\n\n",
                when,
                xnode_notification_history[ i ].source,
                xnode_notification_history[ i ].title,
                xnode_notification_history[ i ].body
            );
            const size_t line_len = strlen( line );
            const size_t copy_len = min( line_len, sizeof( text ) - used - 1 );
            memcpy( text + used, line, copy_len );
            used += copy_len;
            text[ used ] = '\0';
        }
        lv_label_set_text( xnode_notifications_label, text );
    }

    void xnode_notifications_open_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                xnode_notifications_render();
                mainbar_jump_to_tilenumber( xnode_notifications_tile_num, LV_ANIM_OFF, true );
                break;
            default:
                break;
        }
    }

    void xnode_notifications_exit_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_CLICKED:
                mainbar_jump_back();
                break;
            default:
                break;
        }
    }

    void xnode_notifications_enable_event_cb( lv_obj_t *obj, lv_event_t event ) {
        switch( event ) {
            case LV_EVENT_VALUE_CHANGED:
                xnode_notifications_set_enabled( lv_switch_get_state( obj ) );
                break;
            default:
                break;
        }
    }

    void xnode_notifications_activate_cb( void ) {
        xnode_notifications_render();
    }

    bool xnode_notifications_button_event_cb( EventBits_t event, void *arg ) {
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

void xnode_notifications_tile_setup( void ) {
    xnode_notifications_ensure_config();

    xnode_notifications_tile_num = mainbar_add_app_tile( 1, 1, "Alert Summary" );
    xnode_notifications_tile = mainbar_get_tile_obj( xnode_notifications_tile_num );
    lv_obj_add_style( xnode_notifications_tile, LV_OBJ_PART_MAIN, APP_STYLE );

    lv_obj_t *header = wf_add_settings_header( xnode_notifications_tile, "XNODE alerts", xnode_notifications_exit_event_cb );
    lv_obj_align( header, xnode_notifications_tile, LV_ALIGN_IN_TOP_LEFT, THEME_PADDING, THEME_PADDING );

    lv_obj_t *enable_cont = wf_add_labeled_switch(
        xnode_notifications_tile,
        "show pushed news",
        &xnode_notifications_enabled_switch,
        xnode_notifications_get_enabled(),
        xnode_notifications_enable_event_cb,
        APP_STYLE
    );
    lv_obj_align( enable_cont, header, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    xnode_notifications_page = lv_page_create( xnode_notifications_tile, NULL );
    lv_obj_set_size( xnode_notifications_page, lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 2 ), lv_disp_get_ver_res( NULL ) - ( THEME_CONT_HEIGHT * 3 ) );
    lv_obj_add_style( xnode_notifications_page, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_page_set_scrlbar_mode( xnode_notifications_page, LV_SCRLBAR_MODE_DRAG );
    lv_obj_align( xnode_notifications_page, enable_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    xnode_notifications_label = lv_label_create( xnode_notifications_page, NULL );
    lv_label_set_long_mode( xnode_notifications_label, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( xnode_notifications_label, lv_page_get_width_fit( xnode_notifications_page ) );
    lv_obj_add_style( xnode_notifications_label, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_label_set_text( xnode_notifications_label, "No XNODE news alerts yet." );

    mainbar_add_tile_button_cb( xnode_notifications_tile_num, xnode_notifications_button_event_cb );
    mainbar_add_tile_activate_cb( xnode_notifications_tile_num, xnode_notifications_activate_cb );
    xnode_notifications_app = app_register( "Alert\nSummary", &notification_64px, xnode_notifications_open_event_cb );
}

bool xnode_notifications_push( const char *source, const char *title, const char *body, uint32_t ts ) {
    xnode_notifications_ensure_config();

    const uint32_t limit = xnode_notifications_history_limit();
    xnode_notification_entry_t entry = { 0 };
    entry.ts = ts ? ts : (uint32_t)time( NULL );
    entry.id = ++xnode_notification_sequence ^ entry.ts;
    xnode_notifications_copy_field( entry.source, sizeof( entry.source ), source, "XNODE" );
    xnode_notifications_copy_field( entry.title, sizeof( entry.title ), title, "News" );
    xnode_notifications_copy_field( entry.body, sizeof( entry.body ), body, "" );

    const uint32_t max_index = min( xnode_notification_count, limit - 1 );
    for ( int32_t i = (int32_t)max_index; i > 0; i-- ) {
        xnode_notification_history[ i ] = xnode_notification_history[ i - 1 ];
    }
    xnode_notification_history[ 0 ] = entry;
    if ( xnode_notification_count < limit ) {
        xnode_notification_count++;
    }
    xnode_notifications_save_history_to_config();

    if ( xnode_notifications_config && xnode_notifications_config->enabled ) {
        StaticJsonDocument< 512 > doc;
        char json[ 512 ];

        doc[ "t" ] = "notify";
        doc[ "id" ] = entry.id;
        doc[ "src" ] = entry.source;
        doc[ "title" ] = entry.title;
        doc[ "body" ] = entry.body;

        const size_t json_len = serializeJson( doc, json, sizeof( json ) );
        if ( json_len > 0 && json_len < sizeof( json ) ) {
            bluetooth_message_queue_msg( json );
        }
    }

    return( true );
}

void xnode_notifications_set_enabled( bool enabled ) {
    xnode_notifications_ensure_config();
    if ( !xnode_notifications_config ) {
        return;
    }
    xnode_notifications_config->enabled = enabled;
    xnode_notifications_config->save();
}

bool xnode_notifications_get_enabled( void ) {
    xnode_notifications_ensure_config();
    return( xnode_notifications_config ? xnode_notifications_config->enabled : true );
}

uint32_t xnode_notifications_get_count( void ) {
    return( xnode_notification_count );
}
