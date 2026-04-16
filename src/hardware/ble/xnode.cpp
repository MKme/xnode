#include "config.h"
#include "xnode.h"

#ifndef NATIVE_64BIT

    #include <Arduino.h>
    #include <ArduinoJson.h>
    #include <errno.h>
    #include <sys/time.h>
    #include <sys/stat.h>
    #include <mbedtls/base64.h>

    #include "NimBLEDescriptor.h"

    #include "app/osmmap/osmmap_app_main.h"
    #include "app/osmmap/config/osmmap_config.h"
    #include "app/meshtastic/meshtastic_service.h"
    #include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
    #include "hardware/blectl.h"
    #include "hardware/device.h"
    #include "hardware/sdcard.h"
    #include "hardware/timesync.h"

    namespace {
        constexpr const char *XNODE_SERVICE_UUID = "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0001";
        constexpr const char *XNODE_CHARACTERISTIC_UUID_RX = "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0002";
        constexpr const char *XNODE_CHARACTERISTIC_UUID_TX = "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0003";
        constexpr const char *XNODE_OFFLINE_TILE_ROOT = "/spiffs/osmmap";
        constexpr const char *XNODE_OFFLINE_TILE_PREFIX = "/spiffs/osmmap/";
        constexpr const char *XNODE_OFFLINE_MAP_NAME = "offline from watch flash";
        constexpr const char *XNODE_SEED_TILE_PATH = "/spiffs/osmmap/10/279/373.png";
        constexpr double XNODE_SEED_TILE_LAT = 43.74623;
        constexpr double XNODE_SEED_TILE_LON = -81.70749;
        constexpr uint32_t XNODE_SEED_TILE_ZOOM = 10;
        constexpr size_t XNODE_FRAME_CHUNK = 140;
        constexpr size_t XNODE_MAX_ENCODED = 4096;
        constexpr size_t XNODE_MAX_JSON = 3072;
        constexpr size_t XNODE_QUEUE_DEPTH = 48;
        constexpr size_t XNODE_FRAME_BUFFER = 256;

        extern const uint8_t xnode_seed_tile_start[] asm("_binary_src_assets_xnode_seed_tile_10_279_373_png_start");
        extern const uint8_t xnode_seed_tile_end[] asm("_binary_src_assets_xnode_seed_tile_10_279_373_png_end");

        NimBLECharacteristic *pXnodeTXCharacteristic = NULL;
        NimBLECharacteristic *pXnodeRXCharacteristic = NULL;
        QueueHandle_t xnode_rx_queue = NULL;
        TaskHandle_t xnode_rx_task_handle = NULL;

        typedef struct {
            char text[ XNODE_FRAME_BUFFER ];
        } xnode_rx_frame_t;

        char xnode_last_host_name[ 32 ] = "XTOC";
        char xnode_rx_id[ 24 ] = "";
        uint16_t xnode_rx_total = 0;
        uint16_t xnode_rx_index = 0;
        String xnode_rx_encoded;

        bool xnode_link_ready( void ) {
            return( pXnodeTXCharacteristic && blectl_get_event( BLECTL_CONNECT | BLECTL_AUTHWAIT ) );
        }

        void xnode_reset_rx( void ) {
            xnode_rx_id[ 0 ] = '\0';
            xnode_rx_total = 0;
            xnode_rx_index = 0;
            xnode_rx_encoded = "";
        }

        bool xnode_queue_notification( const char *title, const char *body ) {
            StaticJsonDocument< 384 > doc;
            char json[ 384 ];

            doc[ "t" ] = "notify";
            doc[ "src" ] = "XNODE";
            doc[ "title" ] = title ? title : "XNODE";
            doc[ "body" ] = body ? body : "";

            const size_t json_len = serializeJson( doc, json, sizeof( json ) );
            if ( json_len == 0 || json_len >= sizeof( json ) ) {
                return( false );
            }

            return( bluetooth_message_queue_msg( json ) );
        }

        bool xnode_base64url_encode( const uint8_t *input, size_t input_len, String &output ) {
            const size_t encoded_capacity = ( ( input_len + 2 ) / 3 ) * 4 + 4;
            unsigned char *encoded = (unsigned char *)malloc( encoded_capacity );
            size_t encoded_len = 0;

            if ( !encoded ) {
                return( false );
            }

            const int rc = mbedtls_base64_encode( encoded, encoded_capacity, &encoded_len, input, input_len );
            if ( rc != 0 ) {
                free( encoded );
                return( false );
            }

            for ( size_t i = 0; i < encoded_len; i++ ) {
                if ( encoded[ i ] == '+' ) {
                    encoded[ i ] = '-';
                }
                else if ( encoded[ i ] == '/' ) {
                    encoded[ i ] = '_';
                }
            }

            while ( encoded_len > 0 && encoded[ encoded_len - 1 ] == '=' ) {
                encoded_len--;
            }
            encoded[ encoded_len ] = '\0';

            output = (const char *)encoded;
            free( encoded );
            return( true );
        }

        bool xnode_base64url_decode( const char *input, String &output ) {
            String normalized = input ? input : "";

            normalized.replace( "-", "+" );
            normalized.replace( "_", "/" );
            while ( normalized.length() % 4 ) {
                normalized += "=";
            }

            const size_t decoded_capacity = ( normalized.length() / 4 ) * 3 + 4;
            unsigned char *decoded = (unsigned char *)malloc( decoded_capacity );
            size_t decoded_len = 0;

            if ( !decoded ) {
                return( false );
            }

            const int rc = mbedtls_base64_decode(
                decoded,
                decoded_capacity,
                &decoded_len,
                (const unsigned char *)normalized.c_str(),
                normalized.length()
            );

            if ( rc != 0 ) {
                free( decoded );
                return( false );
            }

            output = "";
            output.reserve( decoded_len + 1 );
            for ( size_t i = 0; i < decoded_len; i++ ) {
                output += (char)decoded[ i ];
            }
            free( decoded );
            return( true );
        }

        bool xnode_base64url_decode_bytes( const char *input, uint8_t **output, size_t *output_len ) {
            String normalized = input ? input : "";
            uint8_t *decoded = NULL;
            size_t decoded_capacity = 0;
            size_t decoded_len = 0;

            if ( output ) {
                *output = NULL;
            }
            if ( output_len ) {
                *output_len = 0;
            }

            normalized.replace( "-", "+" );
            normalized.replace( "_", "/" );
            while ( normalized.length() % 4 ) {
                normalized += "=";
            }

            decoded_capacity = ( normalized.length() / 4 ) * 3 + 4;
            decoded = (uint8_t *)malloc( decoded_capacity );

            if ( !decoded ) {
                return( false );
            }

            if ( mbedtls_base64_decode(
                     decoded,
                     decoded_capacity,
                     &decoded_len,
                     (const unsigned char *)normalized.c_str(),
                     normalized.length()
                 ) != 0 ) {
                free( decoded );
                return( false );
            }

            if ( output ) {
                *output = decoded;
            }
            else {
                free( decoded );
            }
            if ( output_len ) {
                *output_len = decoded_len;
            }
            return( true );
        }

        bool xnode_send_json_text( const String &json ) {
            if ( !xnode_link_ready() ) {
                return( false );
            }

            String encoded;

            if ( json.length() == 0 || json.length() > XNODE_MAX_JSON ) {
                return( false );
            }

            if ( !xnode_base64url_encode( (const uint8_t *)json.c_str(), json.length(), encoded ) ) {
                return( false );
            }

            if ( encoded.length() == 0 || encoded.length() > XNODE_MAX_ENCODED ) {
                return( false );
            }

            char frame_id[ 20 ];
            snprintf( frame_id, sizeof( frame_id ), "%08lx%04x", (unsigned long)millis(), (unsigned int)( esp_random() & 0xFFFF ) );

            const size_t total = encoded.length() == 0 ? 1 : ( ( encoded.length() + XNODE_FRAME_CHUNK - 1 ) / XNODE_FRAME_CHUNK );
            for ( size_t i = 0; i < total; i++ ) {
                const size_t chunk_start = i * XNODE_FRAME_CHUNK;
                const size_t chunk_len = min( (size_t)XNODE_FRAME_CHUNK, encoded.length() - chunk_start );
                String frame = String( frame_id ) + ":" + String( i + 1 ) + ":" + String( total ) + ":" +
                               encoded.substring( chunk_start, chunk_start + chunk_len );

                pXnodeTXCharacteristic->notify( (const uint8_t *)frame.c_str(), frame.length() );
                delay( 12 );
            }

            return( true );
        }

        bool xnode_send_event( const char *type, JsonVariantConst payload ) {
            DynamicJsonDocument doc( 1536 );
            String json;

            doc[ "type" ] = type;
            doc[ "payload" ] = payload;
            serializeJson( doc, json );
            return( xnode_send_json_text( json ) );
        }

        bool xnode_send_status_event( const char *status, const char *name, const char *tile_root ) {
            StaticJsonDocument< 384 > payload;

            payload[ "status" ] = status ? status : "unknown";
            if ( name && name[ 0 ] ) {
                payload[ "name" ] = name;
            }
            if ( tile_root && tile_root[ 0 ] ) {
                payload[ "tileRoot" ] = tile_root;
            }
            return( xnode_send_event( "basemapStatus", payload ) );
        }

        bool xnode_watch_path_valid( const char *watch_path ) {
            if ( !watch_path ) {
                return( false );
            }

            if ( strncmp( watch_path, XNODE_OFFLINE_TILE_PREFIX, strlen( XNODE_OFFLINE_TILE_PREFIX ) ) != 0 ) {
                return( false );
            }

            if ( strstr( watch_path, ".." ) ) {
                return( false );
            }

            return( true );
        }

        bool xnode_create_dir_if_missing( const char *path ) {
            struct stat st;

            if ( !path || !path[ 0 ] ) {
                return( false );
            }

            if ( stat( path, &st ) == 0 ) {
                return( S_ISDIR( st.st_mode ) );
            }

            if ( mkdir( path, 0777 ) == 0 ) {
                return( true );
            }

            return( errno == EEXIST );
        }

        bool xnode_seed_default_basemap_tile( void ) {
            struct stat st;
            FILE *file = NULL;
            const size_t tile_len = (size_t)( xnode_seed_tile_end - xnode_seed_tile_start );

            if ( tile_len == 0 ) {
                return( false );
            }

            if ( stat( XNODE_SEED_TILE_PATH, &st ) == 0 && (size_t)st.st_size == tile_len ) {
                osmmap_apply_watch_basemap( XNODE_OFFLINE_MAP_NAME, XNODE_SEED_TILE_LON, XNODE_SEED_TILE_LAT, XNODE_SEED_TILE_ZOOM );
                return( true );
            }

            remove( XNODE_SEED_TILE_PATH );
            file = fopen( XNODE_SEED_TILE_PATH, "wb" );
            if ( !file ) {
                return( false );
            }
            if ( fwrite( xnode_seed_tile_start, 1, tile_len, file ) != tile_len ) {
                fclose( file );
                return( false );
            }
            fclose( file );
            osmmap_apply_watch_basemap( XNODE_OFFLINE_MAP_NAME, XNODE_SEED_TILE_LON, XNODE_SEED_TILE_LAT, XNODE_SEED_TILE_ZOOM );
            return( true );
        }

        bool xnode_write_file_chunk( const char *filepath, const uint8_t *data, size_t data_len, bool append, size_t offset, char *detail, size_t detail_size ) {
            FILE *file = NULL;
            size_t written = 0;
            struct stat st;

            if ( detail && detail_size ) {
                detail[ 0 ] = '\0';
            }

            if ( !xnode_watch_path_valid( filepath ) ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "path-invalid %s", filepath ? filepath : "" );
                }
                return( false );
            }
            if ( !data || data_len == 0 ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "empty-payload %s", filepath );
                }
                return( false );
            }
            if ( append ) {
                if ( stat( filepath, &st ) != 0 ) {
                    if ( detail && detail_size ) {
                        snprintf( detail, detail_size, "offset-missing %s want=%u", filepath, (unsigned)offset );
                    }
                    return( false );
                }
                if ( (size_t)st.st_size != offset ) {
                    if ( detail && detail_size ) {
                        snprintf( detail, detail_size, "offset-mismatch %s have=%u want=%u", filepath, (unsigned)st.st_size, (unsigned)offset );
                    }
                    return( false );
                }
            }
            else if ( offset != 0 ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "offset-invalid %s want=0 have=%u", filepath, (unsigned)offset );
                }
                return( false );
            }
            if ( !append ) {
                remove( filepath );
            }

            file = fopen( filepath, append ? "ab" : "wb" );
            if ( !file ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "open-failed %s errno=%d", filepath, errno );
                }
                return( false );
            }

            written = fwrite( data, 1, data_len, file );
            fclose( file );
            if ( written != data_len && detail && detail_size ) {
                snprintf( detail, detail_size, "write-short %s %u/%u", filepath, (unsigned)written, (unsigned)data_len );
            }
            return( written == data_len );
        }

        bool xnode_apply_time_payload( JsonObjectConst payload ) {
            if ( payload[ "ts" ].isNull() ) {
                return( false );
            }

            double ts_value = payload[ "ts" ] | 0.0;
            const char *timezone_name = payload[ "timezoneName" ] | "";
            const char *timezone_rule = payload[ "timezoneRule" ] | "";
            struct timeval now_value;

            if ( ts_value <= 0.0 ) {
                return( false );
            }

            if ( ts_value > 1000000000000.0 ) {
                ts_value /= 1000.0;
            }

            if ( timezone_name[ 0 ] ) {
                timesync_set_timezone_name( (char *)timezone_name );
            }

            if ( timezone_rule[ 0 ] ) {
                timesync_set_timezone_rule( timezone_rule );
            }

            now_value.tv_sec = (time_t)ts_value;
            now_value.tv_usec = 0;
            return( timesync_apply_external_time( now_value.tv_sec ) );
        }

        bool xnode_send_hello_ack( void ) {
            StaticJsonDocument< 512 > payload;
            JsonArray capabilities = payload.createNestedArray( "capabilities" );

            payload[ "deviceName" ] = device_get_name();
            payload[ "protocolVersion" ] = 1;
            payload[ "firmware" ] = "My-TTGO-Watch-Gen3";
            payload[ "hardware" ] = HARDWARE_NAME;
            payload[ "meshReady" ] = meshtastic_service_is_ready();
            payload[ "meshStatus" ] = meshtastic_service_get_status();
            payload[ "nodeId" ] = meshtastic_service_get_node_id();
            capabilities.add( "sync" );
            capabilities.add( "location" );
            capabilities.add( "meshtastic" );
            capabilities.add( "basemap" );
            capabilities.add( "mapOverlay" );
            capabilities.add( "ble" );

            return( xnode_send_event( "helloAck", payload ) );
        }

        bool xnode_apply_location_payload( JsonObjectConst payload, bool notify_user ) {
            const double lat = payload[ "lat" ].isNull() ? ( payload[ "latitude" ] | 999.0 ) : ( payload[ "lat" ] | 999.0 );
            const double lon = payload[ "lon" ].isNull() ? ( payload[ "longitude" ] | 999.0 ) : ( payload[ "lon" ] | 999.0 );
            const char *label = payload[ "label" ].isNull() ? ( payload[ "sharedBy" ] | xnode_last_host_name ) : ( payload[ "label" ] | xnode_last_host_name );

            if ( lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ) {
                return( false );
            }

            osmmap_set_external_marker( lon, lat, label ? label : "XTOC" );

            if ( notify_user ) {
                char body[ 128 ];

                snprintf( body, sizeof( body ), "%.5f %.5f", lat, lon );
                xnode_queue_notification( label ? label : "Location", body );
            }

            return( true );
        }

        const char *xnode_overlay_kind_for_template( uint32_t template_id ) {
            switch ( template_id ) {
                case 1:  return( "sitrep" );
                case 2:  return( "contact" );
                case 3:  return( "task" );
                case 4:  return( "checkin" );
                case 5:  return( "resource" );
                case 6:  return( "asset" );
                case 7:  return( "zone" );
                case 8:  return( "mission" );
                case 9:  return( "event" );
                case 10: return( "phaseline" );
                case 11: return( "sentinel" );
                case 12: return( "route" );
                default: return( NULL );
            }
        }

        bool xnode_apply_overlay_item_payload( JsonObjectConst item ) {
            const char *key = item[ "key" ] | "";
            const char *kind = item[ "kind" ] | "";
            const char *label = item[ "label" ].isNull() ? ( item[ "summary" ] | ( kind ? kind : "" ) ) : ( item[ "label" ] | ( kind ? kind : "" ) );
            const double lat = item[ "lat" ].isNull() ? ( item[ "latitude" ] | 999.0 ) : ( item[ "lat" ] | 999.0 );
            const double lon = item[ "lon" ].isNull() ? ( item[ "longitude" ] | 999.0 ) : ( item[ "lon" ] | 999.0 );
            const uint32_t updated_at = item[ "updatedAt" ].isNull() ? ( item[ "packetAt" ] | 0 ) : ( item[ "updatedAt" ] | 0 );

            if ( !key[ 0 ] || !kind[ 0 ] ) {
                return( false );
            }
            if ( lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ) {
                return( false );
            }

            osmmap_upsert_overlay_item( key, kind, lon, lat, label, updated_at );
            return( true );
        }

        void xnode_handle_command( DynamicJsonDocument &doc ) {
            const char *type = doc[ "type" ] | "";
            JsonObjectConst payload = doc[ "payload" ].as<JsonObjectConst>();

            if ( payload[ "deviceName" ].is<const char *>() ) {
                strlcpy( xnode_last_host_name, payload[ "deviceName" ], sizeof( xnode_last_host_name ) );
            }

            if ( strcmp( type, "hello" ) == 0 ) {
                xnode_send_hello_ack();
                return;
            }

            if ( strcmp( type, "syncState" ) == 0 ) {
                StaticJsonDocument< 384 > reply;
                const bool replace = payload[ "replace" ] | false;

                if ( payload.containsKey( "location" ) ) {
                    JsonObjectConst location = payload[ "location" ].as<JsonObjectConst>();
                    xnode_apply_location_payload( location, false );
                    xnode_apply_time_payload( location );
                }
                if ( payload.containsKey( "basemap" ) ) {
                    JsonObjectConst basemap = payload[ "basemap" ].as<JsonObjectConst>();
                    xnode_send_status_event( "profile-staged", basemap[ "name" ] | "", XNODE_OFFLINE_TILE_ROOT );
                }
                if ( replace ) {
                    osmmap_clear_overlay_items();
                }

                reply[ "packetCount" ] = payload[ "packetCount" ] | 0;
                reply[ "overlayCount" ] = osmmap_overlay_item_count();
                reply[ "meshCount" ] = payload[ "meshCount" ] | 0;
                reply[ "replace" ] = replace;
                xnode_send_event( "syncAck", reply );
                return;
            }

            if ( strcmp( type, "overlayBatch" ) == 0 ) {
                StaticJsonDocument< 256 > reply;
                JsonArrayConst items = payload[ "items" ].as<JsonArrayConst>();
                size_t applied = 0;

                if ( !items.isNull() ) {
                    for ( JsonVariantConst raw_item : items ) {
                        JsonObjectConst item = raw_item.as<JsonObjectConst>();

                        if ( item.isNull() ) {
                            continue;
                        }
                        if ( xnode_apply_overlay_item_payload( item ) ) {
                            applied++;
                        }
                    }
                }
                reply[ "count" ] = applied;
                reply[ "overlayCount" ] = osmmap_overlay_item_count();
                xnode_send_event( "overlayBatchAck", reply );
                return;
            }

            if ( strcmp( type, "packetBatch" ) == 0 ) {
                StaticJsonDocument< 256 > reply;
                JsonArrayConst packets = payload[ "packets" ].as<JsonArrayConst>();
                size_t applied = 0;

                if ( !packets.isNull() ) {
                    for ( JsonVariantConst raw_packet : packets ) {
                        JsonObjectConst packet = raw_packet.as<JsonObjectConst>();
                        const char *key = packet[ "key" ] | "";
                        const char *kind = xnode_overlay_kind_for_template( packet[ "templateId" ] | 0 );
                        const char *label = packet[ "summary" ] | ( kind ? kind : "" );
                        const double lat = packet[ "lat" ].isNull() ? ( packet[ "latitude" ] | 999.0 ) : ( packet[ "lat" ] | 999.0 );
                        const double lon = packet[ "lon" ].isNull() ? ( packet[ "longitude" ] | 999.0 ) : ( packet[ "lon" ] | 999.0 );
                        const uint32_t packet_at = packet[ "packetAt" ] | 0;
                        char overlay_key[ 64 ] = { 0 };

                        if ( !kind || !key[ 0 ] ) {
                            continue;
                        }
                        if ( lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ) {
                            continue;
                        }
                        snprintf( overlay_key, sizeof( overlay_key ), "packet:%s", key );
                        osmmap_upsert_overlay_item( overlay_key, kind, lon, lat, label, packet_at );
                        applied++;
                    }
                }

                reply[ "count" ] = applied;
                reply[ "overlayCount" ] = osmmap_overlay_item_count();
                xnode_send_event( "packetBatchAck", reply );
                return;
            }

            if ( strcmp( type, "packetLine" ) == 0 ) {
                const char *text = payload[ "text" ] | "";

                if ( text[ 0 ] ) {
                    xnode_queue_notification( "XTOC Packet", text );
                }
                return;
            }

            if ( strcmp( type, "meshtasticRx" ) == 0 ) {
                const char *text = payload[ "text" ] | "";
                const char *from = payload[ "from" ] | "Meshtastic";

                if ( text[ 0 ] ) {
                    xnode_queue_notification( from, text );
                }
                return;
            }

            if ( strcmp( type, "location" ) == 0 ) {
                StaticJsonDocument< 256 > reply;
                const double lat = payload[ "lat" ].isNull() ? ( payload[ "latitude" ] | 0.0 ) : ( payload[ "lat" ] | 0.0 );
                const double lon = payload[ "lon" ].isNull() ? ( payload[ "longitude" ] | 0.0 ) : ( payload[ "lon" ] | 0.0 );
                const char *label = payload[ "label" ].isNull() ? ( payload[ "sharedBy" ] | xnode_last_host_name ) : ( payload[ "label" ] | xnode_last_host_name );
                const bool time_updated = xnode_apply_time_payload( payload );

                if ( xnode_apply_location_payload( payload, false ) ) {
                    reply[ "lat" ] = lat;
                    reply[ "lon" ] = lon;
                    reply[ "label" ] = label;
                    reply[ "timeUpdated" ] = time_updated;
                    xnode_send_event( "locationAck", reply );
                }
                return;
            }

            if ( strcmp( type, "mapTile" ) == 0 ) {
                const char *filepath = payload[ "path" ] | "";
                const char *encoded = payload[ "data" ] | "";
                const bool append = payload[ "append" ] | false;
                const size_t offset = payload[ "offset" ] | 0;
                const size_t total_bytes = payload[ "totalBytes" ] | 0;
                char status_name[ 160 ] = { 0 };
                uint8_t *decoded_data = NULL;
                size_t decoded_len = 0;
                struct stat st;

                if ( !filepath[ 0 ] ) {
                    snprintf( status_name, sizeof( status_name ), "path-missing" );
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    return;
                }

                if ( !encoded[ 0 ] ) {
                    snprintf( status_name, sizeof( status_name ), "payload-missing %s", filepath );
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    return;
                }

                if ( !xnode_base64url_decode_bytes( encoded, &decoded_data, &decoded_len ) ) {
                    snprintf( status_name, sizeof( status_name ), "payload-decode-error %s len=%u", filepath, (unsigned)strlen( encoded ) );
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    return;
                }

                if ( xnode_write_file_chunk( filepath, decoded_data, decoded_len, append, offset, status_name, sizeof( status_name ) ) ) {
                    free( decoded_data );
                    if ( total_bytes > 0 && ( offset + decoded_len ) >= total_bytes ) {
                        const bool stat_ok = stat( filepath, &st ) == 0;
                        const size_t stored_size = stat_ok ? (size_t)st.st_size : 0;

                        if ( !stat_ok || stored_size != total_bytes ) {
                            snprintf(
                                status_name,
                                sizeof( status_name ),
                                "size-mismatch %s have=%u want=%u",
                                filepath,
                                (unsigned)stored_size,
                                (unsigned)total_bytes
                            );
                            xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                            return;
                        }
                        snprintf( status_name, sizeof( status_name ), "%s bytes=%u", filepath, (unsigned)total_bytes );
                        xnode_send_status_event( "tile-stored", status_name, XNODE_OFFLINE_TILE_ROOT );
                    }
                    return;
                }
                free( decoded_data );

                if ( status_name[ 0 ] == '\0' ) {
                    snprintf( status_name, sizeof( status_name ), "%s errno=%d", filepath, errno );
                }
                xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                return;
            }

            if ( strcmp( type, "installBasemap" ) == 0 ) {
                JsonObjectConst manifest = payload[ "manifest" ].as<JsonObjectConst>();
                const char *name = manifest[ "name" ] | "Watch Basemap";
                JsonObjectConst center = manifest[ "center" ].as<JsonObjectConst>();
                const double center_lat = center[ "lat" ] | 999.0;
                const double center_lon = center[ "lon" ] | 999.0;
                const uint32_t center_zoom = center[ "zoom" ] | ( manifest[ "minZoom" ] | 10 );
                osmmap_config_t config;
                char body[ 160 ];

                config.load();
                strlcpy( config.osmmap, XNODE_OFFLINE_MAP_NAME, sizeof( config.osmmap ) );
                config.save();
                if ( !osmmap_apply_watch_basemap( XNODE_OFFLINE_MAP_NAME, center_lon, center_lat, center_zoom ) ) {
                    snprintf( body, sizeof( body ), "profile failed: %s (%s)", name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_queue_notification( "Basemap", body );
                    xnode_send_status_event( "profile-error", name, XNODE_OFFLINE_TILE_ROOT );
                    return;
                }
                sdcard_block_unmounting( false );

                snprintf( body, sizeof( body ), "profile staged: %s (%s)", name, XNODE_OFFLINE_TILE_ROOT );
                xnode_queue_notification( "Basemap", body );
                xnode_send_status_event( "profile-active", name, XNODE_OFFLINE_TILE_ROOT );
                return;
            }
        }

        void xnode_handle_frame( const char *frame ) {
            const char *first = strchr( frame, ':' );
            const char *second = first ? strchr( first + 1, ':' ) : NULL;
            const char *third = second ? strchr( second + 1, ':' ) : NULL;

            if ( !first || !second || !third ) {
                return;
            }

            char id[ sizeof( xnode_rx_id ) ];
            const size_t id_len = min( (size_t)( first - frame ), sizeof( id ) - 1 );
            memcpy( id, frame, id_len );
            id[ id_len ] = '\0';

            const int index = atoi( first + 1 );
            const int total = atoi( second + 1 );
            const char *chunk = third + 1;

            if ( !id[ 0 ] || index < 1 || total < 1 || index > total || !chunk[ 0 ] ) {
                xnode_reset_rx();
                return;
            }

            if ( index == 1 || strcmp( id, xnode_rx_id ) != 0 || total != xnode_rx_total ) {
                xnode_reset_rx();
                strlcpy( xnode_rx_id, id, sizeof( xnode_rx_id ) );
                xnode_rx_total = total;
                xnode_rx_encoded.reserve( min( (size_t)( total * XNODE_FRAME_CHUNK ), (size_t)XNODE_MAX_ENCODED ) );
            }

            if ( strcmp( id, xnode_rx_id ) != 0 || total != xnode_rx_total || index != ( xnode_rx_index + 1 ) ) {
                xnode_reset_rx();
                return;
            }

            if ( xnode_rx_encoded.length() + strlen( chunk ) > XNODE_MAX_ENCODED ) {
                xnode_reset_rx();
                return;
            }

            xnode_rx_encoded += chunk;
            xnode_rx_index = index;

            if ( xnode_rx_index == xnode_rx_total ) {
                String decoded_json;
                DynamicJsonDocument doc( XNODE_MAX_JSON );

                if ( xnode_base64url_decode( xnode_rx_encoded.c_str(), decoded_json ) &&
                     deserializeJson( doc, decoded_json ) == DeserializationError::Ok ) {
                    xnode_handle_command( doc );
                }
                else {
                    xnode_send_status_event( "rx-json-error", "command-parse-failed", XNODE_OFFLINE_TILE_ROOT );
                }
                xnode_reset_rx();
            }
        }

        void xnode_rx_task( void *pvParameters ) {
            xnode_rx_frame_t frame;

            while ( true ) {
                if ( xQueueReceive( xnode_rx_queue, &frame, portMAX_DELAY ) == pdTRUE ) {
                    xnode_handle_frame( frame.text );
                }
            }
        }

        class XnodeCallbacks: public NimBLECharacteristicCallbacks {
            void onWrite( NimBLECharacteristic *pCharacteristic ) {
                const std::string value = pCharacteristic->getValue();
                xnode_rx_frame_t frame = { 0 };

                if ( !value.empty() ) {
                    strlcpy( frame.text, value.c_str(), sizeof( frame.text ) );
                    if ( xnode_rx_queue ) {
                        xQueueSend( xnode_rx_queue, &frame, pdMS_TO_TICKS( 20 ) );
                    }
                }
            };
        };

        XnodeCallbacks xnode_callbacks;
    }

    void xnode_setup( void ) {
        NimBLEServer *pServer = blectl_get_ble_server();
        NimBLEAdvertising *pAdvertising = blectl_get_ble_advertising();
        NimBLEService *pXnodeService = pServer->createService( NimBLEUUID( XNODE_SERVICE_UUID ) );

        xnode_seed_default_basemap_tile();

        if ( !xnode_rx_queue ) {
            xnode_rx_queue = xQueueCreate( XNODE_QUEUE_DEPTH, sizeof( xnode_rx_frame_t ) );
        }
        if ( xnode_rx_queue && !xnode_rx_task_handle ) {
            xTaskCreate( xnode_rx_task, "xnode rx", 8192, NULL, 1, &xnode_rx_task_handle );
        }

        pXnodeTXCharacteristic = pXnodeService->createCharacteristic(
            NimBLEUUID( XNODE_CHARACTERISTIC_UUID_TX ),
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
        );
        pXnodeTXCharacteristic->addDescriptor( new NimBLE2904() );

        pXnodeRXCharacteristic = pXnodeService->createCharacteristic(
            NimBLEUUID( XNODE_CHARACTERISTIC_UUID_RX ),
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
        );
        pXnodeRXCharacteristic->setCallbacks( &xnode_callbacks );

        pXnodeService->start();
        pAdvertising->addServiceUUID( pXnodeService->getUUID() );
    }

    bool xnode_send_meshtastic_rx( const char *from, const char *text ) {
        StaticJsonDocument< 384 > payload;

        payload[ "from" ] = from ? from : "Meshtastic";
        payload[ "text" ] = text ? text : "";
        payload[ "ts" ] = (uint32_t)( millis() / 1000 );
        return( xnode_send_event( "meshtasticRx", payload ) );
    }

    bool xnode_send_location_update( double lat, double lon, const char *label ) {
        StaticJsonDocument< 320 > payload;

        payload[ "lat" ] = lat;
        payload[ "lon" ] = lon;
        payload[ "label" ] = label ? label : "Meshtastic";
        payload[ "ts" ] = (uint32_t)( millis() / 1000 );
        return( xnode_send_event( "location", payload ) );
    }

#else

    void xnode_setup( void ) {
    }

    bool xnode_send_meshtastic_rx( const char *from, const char *text ) {
        return( false );
    }

    bool xnode_send_location_update( double lat, double lon, const char *label ) {
        return( false );
    }

#endif
