#include "xnode_notifications_config.h"

#include <stdio.h>

xnode_notifications_config_t::xnode_notifications_config_t() : BaseJsonConfig( XNODE_NOTIFICATIONS_JSON_CONFIG_FILE ) {
    prettyJson = false;
}

bool xnode_notifications_config_t::onSave( JsonDocument& doc ) {
    doc[ "enabled" ] = enabled;
    doc[ "history_limit" ] = history_limit;
    JsonArray history_array = doc.createNestedArray( "history" );
    const uint8_t count = history_count > XNODE_NOTIFICATIONS_HISTORY_MAX ? XNODE_NOTIFICATIONS_HISTORY_MAX : history_count;

    for ( uint8_t i = 0; i < count; i++ ) {
        JsonObject item = history_array.createNestedObject();
        item[ "ts" ] = history[ i ].ts;
        item[ "id" ] = history[ i ].id;
        item[ "source" ] = history[ i ].source;
        item[ "title" ] = history[ i ].title;
        item[ "body" ] = history[ i ].body;
    }

    return( true );
}

bool xnode_notifications_config_t::onLoad( JsonDocument& doc ) {
    enabled = doc[ "enabled" ] | true;
    history_limit = doc[ "history_limit" ] | XNODE_NOTIFICATIONS_HISTORY_MAX;
    if ( history_limit < 5 ) {
        history_limit = 5;
    }
    if ( history_limit > XNODE_NOTIFICATIONS_HISTORY_MAX ) {
        history_limit = XNODE_NOTIFICATIONS_HISTORY_MAX;
    }

    history_count = 0;
    JsonArrayConst history_array = doc[ "history" ].as<JsonArrayConst>();
    if ( !history_array.isNull() ) {
        for ( JsonVariantConst item_variant : history_array ) {
            JsonObjectConst item = item_variant.as<JsonObjectConst>();

            if ( item.isNull() || history_count >= history_limit ) {
                break;
            }

            history[ history_count ].ts = item[ "ts" ] | 0;
            history[ history_count ].id = item[ "id" ] | 0;
            snprintf( history[ history_count ].source, sizeof( history[ history_count ].source ), "%s", item[ "source" ] | "XNODE" );
            snprintf( history[ history_count ].title, sizeof( history[ history_count ].title ), "%s", item[ "title" ] | "News" );
            snprintf( history[ history_count ].body, sizeof( history[ history_count ].body ), "%s", item[ "body" ] | "" );
            history_count++;
        }
    }

    return( true );
}

bool xnode_notifications_config_t::onDefault( void ) {
    enabled = true;
    history_limit = XNODE_NOTIFICATIONS_HISTORY_MAX;
    history_count = 0;
    for ( uint8_t i = 0; i < XNODE_NOTIFICATIONS_HISTORY_MAX; i++ ) {
        history[ i ].ts = 0;
        history[ i ].id = 0;
        history[ i ].source[ 0 ] = '\0';
        history[ i ].title[ 0 ] = '\0';
        history[ i ].body[ 0 ] = '\0';
    }
    return( true );
}
