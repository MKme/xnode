#include "meshtastic_channels_config.h"

#include <stdio.h>
#include <string.h>

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

meshtastic_channels_config_t::meshtastic_channels_config_t() : BaseJsonConfig( MESHTASTIC_CHANNELS_JSON_CONFIG_FILE ) {
    onDefault();
}

bool meshtastic_channels_config_t::onSave( JsonDocument& doc ) {
    doc[ "activeChannel" ] = active_channel;

    for ( uint8_t i = 0; i < MESHTASTIC_CHANNEL_COUNT; i++ ) {
        doc[ "channels" ][ i ][ "enabled" ] = channels[ i ].enabled;
        doc[ "channels" ][ i ][ "name" ] = channels[ i ].name;
        doc[ "channels" ][ i ][ "psk" ] = channels[ i ].psk;
    }
    return( true );
}

bool meshtastic_channels_config_t::onLoad( JsonDocument& doc ) {
    onDefault();

    active_channel = doc[ "activeChannel" ] | 0;

    JsonArrayConst channel_list = doc[ "channels" ].as<JsonArrayConst>();
    if ( !channel_list.isNull() ) {
        uint8_t index = 0;

        for ( JsonVariantConst entry_variant : channel_list ) {
            JsonObjectConst entry = entry_variant.as<JsonObjectConst>();

            if ( entry.isNull() || index >= MESHTASTIC_CHANNEL_COUNT ) {
                break;
            }

            channels[ index ].enabled = entry[ "enabled" ] | channels[ index ].enabled;
            snprintf( channels[ index ].name, sizeof( channels[ index ].name ), "%s", entry[ "name" ] | channels[ index ].name );
            snprintf( channels[ index ].psk, sizeof( channels[ index ].psk ), "%s", entry[ "psk" ] | channels[ index ].psk );
            index++;
        }
    }

    return( true );
}

bool meshtastic_channels_config_t::onDefault( void ) {
    active_channel = 0;

    for ( uint8_t i = 0; i < MESHTASTIC_CHANNEL_COUNT; i++ ) {
        channels[ i ].enabled = false;
        channels[ i ].name[ 0 ] = '\0';
        channels[ i ].psk[ 0 ] = '\0';
    }

    channels[ 0 ].enabled = true;
    snprintf( channels[ 0 ].name, sizeof( channels[ 0 ].name ), "%s", "LongFast" );
    snprintf( channels[ 0 ].psk, sizeof( channels[ 0 ].psk ), "%s", "AQ==" );
    return( true );
}
