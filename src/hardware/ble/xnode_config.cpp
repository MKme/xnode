#include "xnode_config.h"

#include <stdio.h>

xnode_config_t::xnode_config_t() : BaseJsonConfig( XNODE_JSON_CONFIG_FILE ) {
    onDefault();
}

bool xnode_config_t::onSave( JsonDocument& doc ) {
    doc[ "watchUnitId" ] = watch_unit_id;
    doc[ "sosToUnitId" ] = sos_to_unit_id;
    doc[ "watchUnitLabel" ] = watch_unit_label;
    doc[ "hasLocation" ] = has_location;
    if ( has_location ) {
        doc[ "lat" ] = lat;
        doc[ "lon" ] = lon;
    }
    return( true );
}

bool xnode_config_t::onLoad( JsonDocument& doc ) {
    onDefault();

    int32_t watch_id = doc[ "watchUnitId" ] | 0;
    int32_t sos_id = doc[ "sosToUnitId" ] | 0;

    if ( watch_id < 0 ) {
        watch_id = 0;
    }
    if ( watch_id > 65535 ) {
        watch_id = 65535;
    }
    if ( sos_id < 0 ) {
        sos_id = 0;
    }
    if ( sos_id > 65535 ) {
        sos_id = 65535;
    }

    watch_unit_id = (uint16_t)watch_id;
    sos_to_unit_id = (uint16_t)sos_id;
    snprintf( watch_unit_label, sizeof( watch_unit_label ), "%s", doc[ "watchUnitLabel" ] | "" );

    const double stored_lat = doc[ "lat" ] | 999.0;
    const double stored_lon = doc[ "lon" ] | 999.0;
    has_location = ( doc[ "hasLocation" ] | false ) &&
                   stored_lat >= -90.0 && stored_lat <= 90.0 &&
                   stored_lon >= -180.0 && stored_lon <= 180.0;
    if ( has_location ) {
        lat = stored_lat;
        lon = stored_lon;
    }

    return( true );
}

bool xnode_config_t::onDefault( void ) {
    watch_unit_id = 0;
    sos_to_unit_id = 0;
    watch_unit_label[ 0 ] = '\0';
    has_location = false;
    lat = 0.0;
    lon = 0.0;
    return( true );
}
