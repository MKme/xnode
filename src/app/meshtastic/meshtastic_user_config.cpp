#include "meshtastic_user_config.h"

#include <stdio.h>

meshtastic_user_config_t::meshtastic_user_config_t() : BaseJsonConfig( MESHTASTIC_USER_JSON_CONFIG_FILE ) {
    onDefault();
}

bool meshtastic_user_config_t::onSave( JsonDocument& doc ) {
    doc[ "longName" ] = long_name;
    doc[ "shortName" ] = short_name;
    doc[ "isLicensed" ] = is_licensed;
    doc[ "isUnmessageable" ] = is_unmessageable;
    return( true );
}

bool meshtastic_user_config_t::onLoad( JsonDocument& doc ) {
    onDefault();

    const char *long_value = doc[ "longName" ] | "";
    const char *short_value = doc[ "shortName" ] | "";
    if ( !long_value[ 0 ] ) {
        long_value = doc[ "long_name" ] | "";
    }
    if ( !short_value[ 0 ] ) {
        short_value = doc[ "short_name" ] | "";
    }

    snprintf( long_name, sizeof( long_name ), "%s", long_value );
    snprintf( short_name, sizeof( short_name ), "%s", short_value );
    is_licensed = doc.containsKey( "isLicensed" ) ? ( doc[ "isLicensed" ] | false ) : ( doc[ "is_licensed" ] | false );
    is_unmessageable = doc.containsKey( "isUnmessageable" ) ? ( doc[ "isUnmessageable" ] | false ) : ( doc[ "is_unmessageable" ] | false );
    return( true );
}

bool meshtastic_user_config_t::onDefault( void ) {
    long_name[ 0 ] = '\0';
    short_name[ 0 ] = '\0';
    is_licensed = false;
    is_unmessageable = false;
    return( true );
}
