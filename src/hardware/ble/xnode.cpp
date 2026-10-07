#include "config.h"
#include "xnode.h"
#include "xnode_config.h"

#ifndef NATIVE_64BIT

    #include <Arduino.h>
    #include <atomic>
    #include <ArduinoJson.h>
    #include <cmath>
    #include <errno.h>
    #include <math.h>
    #include <sys/time.h>
    #include <sys/stat.h>
    #include <time.h>
    #include <mbedtls/base64.h>

    #include "NimBLEDescriptor.h"

    #include "app/osmmap/osmmap_app_main.h"
    #include "app/osmmap/config/osmmap_config.h"
    #include "app/meshtastic/meshtastic_service.h"
    #include "app/mesh/mesh_protocol.h"
    #include "app/meshcore/meshcore_service.h"
    #include "utils/json_psram_allocator.h"
    #include "gui/mainbar/app_tile/xnode_notifications/xnode_notifications.h"
    #include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
    #include "hardware/blectl.h"
    #include "hardware/device.h"
    #include "hardware/gpsctl.h"
    #include "hardware/sdcard.h"
    #include "hardware/timesync.h"

    namespace {
        constexpr const char *XNODE_SERVICE_UUID = "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0001";
        constexpr const char *XNODE_CHARACTERISTIC_UUID_RX = "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0002";
        constexpr const char *XNODE_CHARACTERISTIC_UUID_TX = "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0003";
        constexpr const char *XNODE_CHARACTERISTIC_UUID_SECURE = "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0004";
        #if defined( LILYGO_WATCH_ULTRA )
            constexpr const char *XNODE_OFFLINE_TILE_ROOT = "/sd/osmmap";
            constexpr const char *XNODE_OFFLINE_TILE_PREFIX = "/sd/osmmap/";
            constexpr const char *XNODE_OFFLINE_MAP_NAME = "offline from watch sd";
            constexpr const char *XNODE_CURRENT_TILE_PATH = "/sd/osmmap/current.png";
            constexpr const char *XNODE_SEED_TILE_PATH = "/sd/osmmap/10/279/373.png";
            constexpr const char *XNODE_LEGACY_OFFLINE_TILE_PREFIX = "/spiffs/osmmap/";
        #else
            constexpr const char *XNODE_OFFLINE_TILE_ROOT = "/spiffs/osmmap";
            constexpr const char *XNODE_OFFLINE_TILE_PREFIX = "/spiffs/osmmap/";
            constexpr const char *XNODE_OFFLINE_MAP_NAME = "offline from watch flash";
            constexpr const char *XNODE_CURRENT_TILE_PATH = "/spiffs/osmmap/current.png";
            constexpr const char *XNODE_SEED_TILE_PATH = "/spiffs/osmmap/10/279/373.png";
        #endif
        constexpr double XNODE_SEED_TILE_LAT = 43.74623;
        constexpr double XNODE_SEED_TILE_LON = -81.70749;
        constexpr uint32_t XNODE_SEED_TILE_ZOOM = 10;
        constexpr size_t XNODE_FRAME_CHUNK = 140;
        constexpr size_t XNODE_MAX_ENCODED = 8192;
        constexpr size_t XNODE_MAX_JSON = 6144;
        constexpr size_t XNODE_QUEUE_DEPTH = 96;
        constexpr size_t XNODE_FRAME_BUFFER = 256;
        constexpr uint32_t XNODE_LOCATION_SAVE_INTERVAL_MS = 300000;

        extern const uint8_t xnode_seed_tile_start[] asm("_binary_src_assets_xnode_seed_tile_10_279_373_png_start");
        extern const uint8_t xnode_seed_tile_end[] asm("_binary_src_assets_xnode_seed_tile_10_279_373_png_end");

        NimBLECharacteristic *pXnodeTXCharacteristic = NULL;
        NimBLECharacteristic *pXnodeRXCharacteristic = NULL;
        QueueHandle_t xnode_rx_queue = NULL;
        TaskHandle_t xnode_rx_task_handle = NULL;

        typedef struct {
            char text[ XNODE_FRAME_BUFFER ];
            uint32_t connection_epoch;
            uint16_t connection_handle;
            bool authenticated;
        } xnode_rx_frame_t;

        // Disconnects invalidate both queued frames and partially assembled JSON.
        std::atomic<uint32_t> xnode_connection_epoch{1};
        uint32_t xnode_rx_connection_epoch = 0;
        uint16_t xnode_rx_connection_handle = 0xffff;
        bool xnode_rx_authenticated = false;

        char xnode_last_host_name[ 32 ] = "XTOC";
        xnode_config_t xnode_config;
        uint16_t xnode_watch_unit_id = 0;
        uint16_t xnode_sos_to_unit_id = 0;
        char xnode_watch_unit_label[ 32 ] = "";
        double xnode_last_lat = 0.0;
        double xnode_last_lon = 0.0;
        bool xnode_has_location = false;
        uint32_t xnode_last_location_save_ms = 0;
        char xnode_rx_id[ 24 ] = "";
        uint16_t xnode_rx_total = 0;
        uint16_t xnode_rx_index = 0;
        String xnode_rx_encoded;
        char xnode_file_path[ 160 ] = { 0 };
        size_t xnode_file_total = 0;
        size_t xnode_file_offset = 0;
        size_t xnode_file_next_status = 0;
        bool xnode_file_active = false;
        size_t xnode_overlay_sync_expected = 0;
        size_t xnode_overlay_sync_seen = 0;
        bool xnode_overlay_sync_replacing = false;

        bool xnode_link_ready( void ) {
            return( pXnodeTXCharacteristic && blectl_get_event( BLECTL_CONNECT | BLECTL_AUTHWAIT ) );
        }

        void xnode_reset_rx( void ) {
            xnode_rx_id[ 0 ] = '\0';
            xnode_rx_total = 0;
            xnode_rx_index = 0;
            xnode_rx_encoded = "";
            xnode_rx_connection_epoch = 0;
            xnode_rx_connection_handle = 0xffff;
            xnode_rx_authenticated = false;
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

        uint16_t xnode_payload_u16( JsonObjectConst payload, const char *primary_key, const char *fallback_key, uint16_t fallback ) {
            const char *key = NULL;

            if ( primary_key && payload.containsKey( primary_key ) ) {
                key = primary_key;
            }
            else if ( fallback_key && payload.containsKey( fallback_key ) ) {
                key = fallback_key;
            }
            if ( !key ) {
                return( fallback );
            }

            const int32_t value = payload[ key ] | (int32_t)fallback;
            if ( value < 0 ) {
                return( 0 );
            }
            if ( value > 65535 ) {
                return( 65535 );
            }
            return( (uint16_t)value );
        }

        void xnode_load_persistent_config( void ) {
            xnode_config.load();

            xnode_watch_unit_id = xnode_config.watch_unit_id;
            xnode_sos_to_unit_id = xnode_config.sos_to_unit_id;
            strlcpy( xnode_watch_unit_label, xnode_config.watch_unit_label, sizeof( xnode_watch_unit_label ) );
            if ( xnode_config.has_location &&
                 xnode_config.lat >= -90.0 && xnode_config.lat <= 90.0 &&
                 xnode_config.lon >= -180.0 && xnode_config.lon <= 180.0 ) {
                xnode_last_lat = xnode_config.lat;
                xnode_last_lon = xnode_config.lon;
                xnode_has_location = true;
            }
            else {
                xnode_last_lat = 0.0;
                xnode_last_lon = 0.0;
                xnode_has_location = false;
            }
        }

        bool xnode_save_persistent_config( void ) {
            xnode_config.watch_unit_id = xnode_watch_unit_id;
            xnode_config.sos_to_unit_id = xnode_sos_to_unit_id;
            strlcpy( xnode_config.watch_unit_label, xnode_watch_unit_label, sizeof( xnode_config.watch_unit_label ) );
            xnode_config.has_location = xnode_has_location;
            xnode_config.lat = xnode_has_location ? xnode_last_lat : 0.0;
            xnode_config.lon = xnode_has_location ? xnode_last_lon : 0.0;
            return( xnode_config.save() );
        }

        bool xnode_save_persistent_location_if_due( bool force ) {
            const uint32_t now = millis();

            if ( !xnode_has_location ) {
                return( false );
            }
            if ( !force &&
                 xnode_config.has_location &&
                 now - xnode_last_location_save_ms < XNODE_LOCATION_SAVE_INTERVAL_MS ) {
                return( true );
            }

            xnode_last_location_save_ms = now;
            return( xnode_save_persistent_config() );
        }

        bool xnode_apply_sos_config_payload( JsonObjectConst payload ) {
            bool changed = false;

            if ( payload.containsKey( "watchUnitId" ) || payload.containsKey( "unitId" ) ) {
                xnode_watch_unit_id = xnode_payload_u16( payload, "watchUnitId", "unitId", xnode_watch_unit_id );
                changed = true;
            }
            if ( payload.containsKey( "sosToUnitId" ) || payload.containsKey( "toUnitId" ) ) {
                xnode_sos_to_unit_id = xnode_payload_u16( payload, "sosToUnitId", "toUnitId", xnode_sos_to_unit_id );
                changed = true;
            }
            if ( payload[ "watchUnitLabel" ].is<const char *>() ) {
                strlcpy( xnode_watch_unit_label, payload[ "watchUnitLabel" ], sizeof( xnode_watch_unit_label ) );
                changed = true;
            }

            return( changed );
        }

        const char *xnode_protocol_id(mesh_protocol_t protocol) {
            return protocol == MESH_PROTOCOL_MESHCORE ? "meshcore" : "meshtastic";
        }

        bool xnode_parse_protocol(JsonVariantConst value, mesh_protocol_t &protocol) {
            if (!value.is<const char *>()) return false;
            const char *name = value.as<const char *>();
            if (!strcmp(name, "meshtastic")) protocol = MESH_PROTOCOL_MESHTASTIC;
            else if (!strcmp(name, "meshcore")) protocol = MESH_PROTOCOL_MESHCORE;
            else return false;
            return mesh_protocol_is_supported(protocol);
        }

        bool xnode_payload_matches_active_protocol(JsonObjectConst payload) {
            mesh_protocol_t protocol;
            return xnode_parse_protocol(payload["protocol"], protocol) && protocol == mesh_protocol_get_active();
        }

        void xnode_fill_protocol_payload(JsonObject payload) {
            JsonArray available = payload.createNestedArray("meshProtocolsAvailable");
            if (mesh_protocol_is_supported(MESH_PROTOCOL_MESHTASTIC)) available.add("meshtastic");
            if (mesh_protocol_is_supported(MESH_PROTOCOL_MESHCORE)) available.add("meshcore");
            payload["meshProtocolActive"] = xnode_protocol_id(mesh_protocol_get_active());
            payload["meshProtocolSelected"] = xnode_protocol_id(mesh_protocol_get_selected());
            payload["protocolRequiresRestart"] = mesh_protocol_reboot_required();
            payload["radioRequiresRestart"] = meshcore_service_radio_reboot_required();
            payload["meshMode"] = mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE ? "group-broadcast" : "channel-mesh";
            payload["nativeMeshcoreCompanion"] = false;
            payload["meshMutationRequiresPairing"] = true;
            payload["meshSecureConfigVersion"] = 1;
        }

        void xnode_fill_meshcore_user_payload(JsonObject payload) {
            payload["publicKey"] = meshcore_service_get_public_key_hex();
            payload["idType"] = "ed25519-public-key";
            payload["longName"] = meshtastic_service_get_long_name();
        }

        void xnode_fill_meshcore_radio_payload(JsonObject payload, bool active) {
            meshcore_service_radio_config_t config = {};
            if (active) meshcore_service_get_active_radio_config(&config);
            else meshcore_service_get_radio_config(&config);
            payload["frequencyMhz"] = config.frequency_mhz;
            payload["bandwidthKhz"] = config.bandwidth_khz;
            payload["spreadingFactor"] = config.spreading_factor;
            payload["codingRate"] = config.coding_rate;
            payload["txPowerDbm"] = config.tx_power_dbm;
        }

        bool xnode_valid_mesh_name(const char *name, size_t max_bytes) {
            if (!name || !name[0] || strlen(name) > max_bytes) return false;
            for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
                if (*p < 0x20 || *p == ':' || *p == 0x7f) return false;
            return true;
        }

        int xnode_hex_digit(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool xnode_parse_meshcore_channel(JsonObjectConst payload, uint8_t &slot, meshtastic_service_channel_info_t &info) {
            if (!payload["slot"].is<unsigned>() || payload["slot"].as<unsigned>() > 7 || !payload["enabled"].is<bool>()) return false;
            slot = payload["slot"].as<unsigned>();
            memset(&info, 0, sizeof(info));
            info.enabled = payload["enabled"].as<bool>();
            info.role = info.enabled ? (slot == 0 ? MESHTASTIC_SERVICE_CHANNEL_ROLE_PRIMARY : MESHTASTIC_SERVICE_CHANNEL_ROLE_SECONDARY) : MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED;
            if (!info.enabled) return true;
            const char *name = payload["name"] | "";
            const char *key = payload["keyHex"] | "";
            const size_t key_length = strlen(key);
            if (!xnode_valid_mesh_name(name, sizeof(info.name) - 1) || (key_length != 32 && key_length != 64)) return false;
            strlcpy(info.name, name, sizeof(info.name));
            info.psk_len = key_length / 2;
            for (size_t i = 0; i < info.psk_len; ++i) {
                const int hi = xnode_hex_digit(key[2 * i]), lo = xnode_hex_digit(key[2 * i + 1]);
                if (hi < 0 || lo < 0) return false;
                info.psk[i] = (uint8_t)((hi << 4) | lo);
            }
            return true;
        }

        bool xnode_parse_frame_number(const char *begin, const char *end, uint16_t &out) {
            if (!begin || !end || begin >= end || end - begin > 4) return false;
            unsigned value = 0;
            for (const char *p = begin; p < end; ++p) {
                if (*p < '0' || *p > '9') return false;
                value = value * 10 + (unsigned)(*p - '0');
                if (value > XNODE_MAX_ENCODED) return false;
            }
            if (value == 0) return false;
            out = (uint16_t)value;
            return true;
        }

        const char *xnode_mesh_mutation_reply_type(const char *type) {
            if (!strcmp(type, "setMeshProtocol")) return "meshProtocolAck";
            if (!strcmp(type, "meshSend")) return "meshSendAck";
            if (!strcmp(type, "selectMeshChannel")) return "meshChannelAck";
            if (!strcmp(type, "setMeshcoreChannel")) return "meshChannelsAck";
            if (!strcmp(type, "setMeshcoreUser")) return "meshcoreUserAck";
            if (!strcmp(type, "setMeshcoreRadio")) return "meshcoreRadioAck";
            return NULL;
        }

        void xnode_fill_meshtastic_user_payload( JsonObject payload ) {
            meshtastic_service_user_info_t info;

            memset( &info, 0, sizeof( info ) );
            meshtastic_service_get_user_info( &info );
            payload[ "nodeId" ] = meshtastic_service_get_node_id();
            payload[ "longName" ] = info.long_name;
            payload[ "shortName" ] = info.short_name;
            payload[ "isLicensed" ] = info.is_licensed;
            payload[ "isUnmessageable" ] = info.is_unmessageable;
        }

        bool xnode_apply_meshtastic_user_payload( JsonObjectConst payload, bool *broadcast_requested ) {
            meshtastic_service_user_info_t info;

            if ( broadcast_requested ) {
                *broadcast_requested = payload[ "broadcast" ] | false;
            }

            if ( !meshtastic_service_get_user_info( &info ) ) {
                memset( &info, 0, sizeof( info ) );
            }

            if ( payload[ "longName" ].is<const char *>() ) {
                strlcpy( info.long_name, payload[ "longName" ], sizeof( info.long_name ) );
            }
            if ( payload[ "long_name" ].is<const char *>() ) {
                strlcpy( info.long_name, payload[ "long_name" ], sizeof( info.long_name ) );
            }
            if ( payload[ "shortName" ].is<const char *>() ) {
                strlcpy( info.short_name, payload[ "shortName" ], sizeof( info.short_name ) );
            }
            if ( payload[ "short_name" ].is<const char *>() ) {
                strlcpy( info.short_name, payload[ "short_name" ], sizeof( info.short_name ) );
            }
            if ( payload.containsKey( "isLicensed" ) ) {
                info.is_licensed = payload[ "isLicensed" ] | false;
            }
            if ( payload.containsKey( "is_licensed" ) ) {
                info.is_licensed = payload[ "is_licensed" ] | false;
            }
            if ( payload.containsKey( "isUnmessageable" ) ) {
                info.is_unmessageable = payload[ "isUnmessageable" ] | false;
            }
            if ( payload.containsKey( "is_unmessageable" ) ) {
                info.is_unmessageable = payload[ "is_unmessageable" ] | false;
            }

            return( meshtastic_service_set_user_info( &info ) );
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

        void xnode_write_u16_be( uint8_t *out, uint16_t value ) {
            out[ 0 ] = (uint8_t)( ( value >> 8 ) & 0xff );
            out[ 1 ] = (uint8_t)( value & 0xff );
        }

        void xnode_write_u32_be( uint8_t *out, uint32_t value ) {
            out[ 0 ] = (uint8_t)( ( value >> 24 ) & 0xff );
            out[ 1 ] = (uint8_t)( ( value >> 16 ) & 0xff );
            out[ 2 ] = (uint8_t)( ( value >> 8 ) & 0xff );
            out[ 3 ] = (uint8_t)( value & 0xff );
        }

        void xnode_write_i32_be( uint8_t *out, int32_t value ) {
            xnode_write_u32_be( out, (uint32_t)value );
        }

        uint32_t xnode_unix_minutes( void ) {
            const time_t now = time( NULL );
            if ( now <= 0 ) {
                return( 0 );
            }
            return( (uint32_t)( now / 60 ) );
        }

        void xnode_generate_packet_id( char *out, size_t out_size ) {
            static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
            const size_t alphabet_len = sizeof( alphabet ) - 1;

            if ( !out || out_size == 0 ) {
                return;
            }

            const size_t count = out_size > 9 ? 8 : out_size - 1;
            for ( size_t i = 0; i < count; i++ ) {
                out[ i ] = alphabet[ esp_random() % alphabet_len ];
            }
            out[ count ] = '\0';
        }

        bool xnode_make_manual_sos_packet( char *out, size_t out_size ) {
            static const char note[] = "Manual SOS";
            uint8_t buf[ 64 ];
            size_t o = 0;
            String encoded;
            char packet_id[ 9 ] = { 0 };
            const int32_t lat_e5 = (int32_t)lround( xnode_last_lat * 100000.0 );
            const int32_t lon_e5 = (int32_t)lround( xnode_last_lon * 100000.0 );
            const size_t note_len = strlen( note );

            if ( !out || out_size == 0 || xnode_watch_unit_id == 0 || !xnode_has_location ) {
                return( false );
            }

            buf[ o++ ] = 1; // SITREP v1
            xnode_write_u16_be( &buf[ o ], xnode_watch_unit_id );
            o += 2;
            xnode_write_u16_be( &buf[ o ], xnode_sos_to_unit_id );
            o += 2;
            buf[ o++ ] = 0; // P1
            buf[ o++ ] = 1; // HELP
            xnode_write_u32_be( &buf[ o ], xnode_unix_minutes() );
            o += 4;
            buf[ o++ ] = 0x03; // has location + note
            xnode_write_i32_be( &buf[ o ], lat_e5 );
            o += 4;
            xnode_write_i32_be( &buf[ o ], lon_e5 );
            o += 4;
            buf[ o++ ] = (uint8_t)note_len;
            memcpy( &buf[ o ], note, note_len );
            o += note_len;

            if ( !xnode_base64url_encode( buf, o, encoded ) ) {
                return( false );
            }

            xnode_generate_packet_id( packet_id, sizeof( packet_id ) );
            const int written = snprintf( out, out_size, "X1.1.C.%s.1/1.%s", packet_id, encoded.c_str() );
            return( written > 0 && (size_t)written < out_size );
        }

        bool xnode_make_manual_checkin_packet( char *out, size_t out_size ) {
            uint8_t buf[ 16 ];
            size_t o = 0;
            String encoded;
            char packet_id[ 9 ] = { 0 };
            const int32_t lat_e5 = (int32_t)lround( xnode_last_lat * 100000.0 );
            const int32_t lon_e5 = (int32_t)lround( xnode_last_lon * 100000.0 );

            if ( !out || out_size == 0 || xnode_watch_unit_id == 0 || !xnode_has_location ) {
                return( false );
            }

            buf[ o++ ] = 1; // CHECKIN/LOC v1
            xnode_write_u16_be( &buf[ o ], xnode_watch_unit_id );
            o += 2;
            xnode_write_i32_be( &buf[ o ], lat_e5 );
            o += 4;
            xnode_write_i32_be( &buf[ o ], lon_e5 );
            o += 4;
            xnode_write_u32_be( &buf[ o ], xnode_unix_minutes() );
            o += 4;
            buf[ o++ ] = 0; // OK

            if ( !xnode_base64url_encode( buf, o, encoded ) ) {
                return( false );
            }

            xnode_generate_packet_id( packet_id, sizeof( packet_id ) );
            const int written = snprintf( out, out_size, "X1.4.C.%s.1/1.%s", packet_id, encoded.c_str() );
            return( written > 0 && (size_t)written < out_size );
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
            SpiRamJsonDocument doc( XNODE_MAX_JSON );
            String json;

            doc[ "type" ] = type;
            doc[ "payload" ] = payload;
            if (doc.overflowed()) return false;
            serializeJson( doc, json );
            return( xnode_send_json_text( json ) );
        }

        bool xnode_send_status_event( const char *status, const char *name, const char *tile_root, size_t bytes = 0, size_t total = 0, const char *path = NULL, const char *hash = NULL ) {
            StaticJsonDocument< 512 > payload;

            payload[ "status" ] = status ? status : "unknown";
            if ( name && name[ 0 ] ) {
                payload[ "name" ] = name;
            }
            if ( tile_root && tile_root[ 0 ] ) {
                payload[ "tileRoot" ] = tile_root;
            }
            if ( path && path[ 0 ] ) {
                payload[ "path" ] = path;
            }
            if ( total > 0 ) {
                payload[ "bytes" ] = (uint32_t)bytes;
                payload[ "totalBytes" ] = (uint32_t)total;
            }
            if ( hash && hash[ 0 ] ) {
                payload[ "hash" ] = hash;
            }
            return( xnode_send_event( "basemapStatus", payload ) );
        }

        bool xnode_send_request_sync( const char *reason ) {
            StaticJsonDocument< 128 > payload;

            payload[ "reason" ] = reason && reason[ 0 ] ? reason : "watch-request";
            payload[ "overlayCount" ] = osmmap_overlay_item_count();
            return( xnode_send_event( "requestSync", payload ) );
        }

        void xnode_set_basemap_storage_busy( bool busy ) {
#if defined( LILYGO_WATCH_ULTRA )
            sdcard_block_unmounting( busy );
#else
            (void)busy;
#endif
        }

        bool xnode_resolve_watch_path( const char *watch_path, char *resolved_path, size_t resolved_size, char *detail, size_t detail_size ) {
            if ( resolved_path && resolved_size ) {
                resolved_path[ 0 ] = '\0';
            }
            if ( detail && detail_size ) {
                detail[ 0 ] = '\0';
            }

            if ( !watch_path || !watch_path[ 0 ] ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "path-missing" );
                }
                return( false );
            }

            if ( strstr( watch_path, ".." ) ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "path-invalid %s", watch_path );
                }
                return( false );
            }

            if ( strncmp( watch_path, XNODE_OFFLINE_TILE_PREFIX, strlen( XNODE_OFFLINE_TILE_PREFIX ) ) != 0 ) {
#if defined( LILYGO_WATCH_ULTRA )
                if ( strncmp( watch_path, XNODE_LEGACY_OFFLINE_TILE_PREFIX, strlen( XNODE_LEGACY_OFFLINE_TILE_PREFIX ) ) == 0 ) {
                    const char *suffix = watch_path + strlen( XNODE_LEGACY_OFFLINE_TILE_PREFIX );
                    const int written = snprintf( resolved_path, resolved_size, "%s%s", XNODE_OFFLINE_TILE_PREFIX, suffix );
                    if ( written < 0 || (size_t)written >= resolved_size ) {
                        if ( detail && detail_size ) {
                            snprintf( detail, detail_size, "path-too-long %s", watch_path );
                        }
                        return( false );
                    }
                    return( true );
                }
#endif
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "path-invalid %s", watch_path );
                }
                return( false );
            }

            if ( strlcpy( resolved_path, watch_path, resolved_size ) >= resolved_size ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "path-too-long %s", watch_path );
                }
                return( false );
            }

            return( true );
        }

        bool xnode_watch_path_valid( const char *watch_path ) {
            char resolved_path[ sizeof( xnode_file_path ) ] = { 0 };

            return( xnode_resolve_watch_path( watch_path, resolved_path, sizeof( resolved_path ), NULL, 0 ) );
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

        bool xnode_ensure_watch_parent_dirs( const char *filepath, char *detail, size_t detail_size ) {
            char resolved_filepath[ sizeof( xnode_file_path ) ] = { 0 };

            if ( !xnode_resolve_watch_path( filepath, resolved_filepath, sizeof( resolved_filepath ), detail, detail_size ) ) {
                return( false );
            }
            if ( detail && detail_size ) {
                detail[ 0 ] = '\0';
            }
#if defined( LILYGO_WATCH_ULTRA )
            char path[ sizeof( xnode_file_path ) ] = { 0 };
            char *cursor = NULL;

            strlcpy( path, resolved_filepath, sizeof( path ) );
            cursor = strrchr( path, '/' );
            if ( !cursor ) {
                return( true );
            }
            *cursor = '\0';
            for ( cursor = path + 1; *cursor; cursor++ ) {
                if ( *cursor == '/' ) {
                    *cursor = '\0';
                    if ( !xnode_create_dir_if_missing( path ) ) {
                        if ( detail && detail_size ) {
                            snprintf( detail, detail_size, "mkdir-failed %s errno=%d", path, errno );
                        }
                        return( false );
                    }
                    *cursor = '/';
                }
            }
            if ( !xnode_create_dir_if_missing( path ) ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "mkdir-failed %s errno=%d", path, errno );
                }
                return( false );
            }
            return( true );
#else
            /*
             * ESP32 SPIFFS has no real directory tree. Paths with slashes are valid
             * filenames in the mounted namespace, but mkdir("/spiffs/osmmap") fails
             * on device and blocks map install before the first write.
             */
            return( true );
#endif
        }

        bool xnode_file_hash32( const char *filepath, uint32_t *hash, size_t *bytes ) {
            FILE *file = NULL;
            uint8_t buffer[ 512 ];
            size_t total = 0;
            uint32_t next_hash = 2166136261UL;

            if ( hash ) {
                *hash = 0;
            }
            if ( bytes ) {
                *bytes = 0;
            }
            if ( !filepath || !filepath[ 0 ] ) {
                return( false );
            }

            file = fopen( filepath, "rb" );
            if ( !file ) {
                return( false );
            }

            while ( true ) {
                const size_t read_len = fread( buffer, 1, sizeof( buffer ), file );
                for ( size_t i = 0; i < read_len; i++ ) {
                    next_hash ^= buffer[ i ];
                    next_hash *= 16777619UL;
                }
                total += read_len;
                if ( read_len < sizeof( buffer ) ) {
                    break;
                }
            }

            const bool ok = ferror( file ) == 0;
            fclose( file );
            if ( !ok ) {
                return( false );
            }
            if ( hash ) {
                *hash = next_hash;
            }
            if ( bytes ) {
                *bytes = total;
            }
            return( true );
        }

        void xnode_hash32_hex( uint32_t hash, char *out, size_t out_size ) {
            if ( out && out_size ) {
                snprintf( out, out_size, "%08lx", (unsigned long)hash );
            }
        }

        bool xnode_seed_default_basemap_tile( void ) {
            struct stat st;
            FILE *file = NULL;
            const size_t tile_len = (size_t)( xnode_seed_tile_end - xnode_seed_tile_start );
            osmmap_config_t config;
            char detail[ 160 ] = { 0 };

            config.load();
            if ( !strcmp( config.osmmap, XNODE_OFFLINE_MAP_NAME ) && config.watch_flash_basemap_valid ) {
                return( true );
            }

            if ( tile_len == 0 ) {
                return( false );
            }

            if ( stat( XNODE_SEED_TILE_PATH, &st ) == 0 && (size_t)st.st_size == tile_len ) {
                return( true );
            }

            if ( !xnode_ensure_watch_parent_dirs( XNODE_SEED_TILE_PATH, detail, sizeof( detail ) ) ) {
                return( false );
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
            return( true );
        }

        bool xnode_write_file_chunk( const char *filepath, const uint8_t *data, size_t data_len, bool append, size_t offset, char *detail, size_t detail_size ) {
            FILE *file = NULL;
            size_t written = 0;
            struct stat st;
            char resolved_filepath[ sizeof( xnode_file_path ) ] = { 0 };

            if ( detail && detail_size ) {
                detail[ 0 ] = '\0';
            }

            if ( !xnode_resolve_watch_path( filepath, resolved_filepath, sizeof( resolved_filepath ), detail, detail_size ) ) {
                return( false );
            }
            if ( !data || data_len == 0 ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "empty-payload %s", resolved_filepath );
                }
                return( false );
            }
            if ( append ) {
                if ( stat( resolved_filepath, &st ) != 0 ) {
                    if ( detail && detail_size ) {
                        snprintf( detail, detail_size, "offset-missing %s want=%u", resolved_filepath, (unsigned)offset );
                    }
                    return( false );
                }
                if ( (size_t)st.st_size != offset ) {
                    if ( detail && detail_size ) {
                        snprintf( detail, detail_size, "offset-mismatch %s have=%u want=%u", resolved_filepath, (unsigned)st.st_size, (unsigned)offset );
                    }
                    return( false );
                }
            }
            else if ( offset != 0 ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "offset-invalid %s want=0 have=%u", resolved_filepath, (unsigned)offset );
                }
                return( false );
            }
            if ( !xnode_ensure_watch_parent_dirs( resolved_filepath, detail, detail_size ) ) {
                return( false );
            }
            if ( !append ) {
                osmmap_prepare_watch_basemap_file_replace( resolved_filepath );
                remove( resolved_filepath );
            }

            file = fopen( resolved_filepath, append ? "ab" : "wb" );
            if ( !file ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "open-failed %s errno=%d", resolved_filepath, errno );
                }
                return( false );
            }

            written = fwrite( data, 1, data_len, file );
            fclose( file );
            if ( written != data_len && detail && detail_size ) {
                snprintf( detail, detail_size, "write-short %s %u/%u", resolved_filepath, (unsigned)written, (unsigned)data_len );
            }
            return( written == data_len );
        }

        void xnode_reset_file_transfer( void ) {
            xnode_file_path[ 0 ] = '\0';
            xnode_file_total = 0;
            xnode_file_offset = 0;
            xnode_file_next_status = 0;
            xnode_file_active = false;
        }

        bool xnode_begin_file_transfer( const char *filepath, size_t total_bytes, char *detail, size_t detail_size ) {
            char resolved_filepath[ sizeof( xnode_file_path ) ] = { 0 };

            if ( detail && detail_size ) {
                detail[ 0 ] = '\0';
            }

            if ( !xnode_resolve_watch_path( filepath, resolved_filepath, sizeof( resolved_filepath ), detail, detail_size ) ) {
                return( false );
            }
            if ( total_bytes == 0 || total_bytes > 512000 ) {
                if ( detail && detail_size ) {
                    snprintf( detail, detail_size, "size-invalid %u", (unsigned)total_bytes );
                }
                return( false );
            }
            xnode_set_basemap_storage_busy( true );
            if ( !xnode_ensure_watch_parent_dirs( resolved_filepath, detail, detail_size ) ) {
                xnode_set_basemap_storage_busy( false );
                return( false );
            }
            osmmap_prepare_watch_basemap_file_replace( resolved_filepath );
            remove( resolved_filepath );
            strlcpy( xnode_file_path, resolved_filepath, sizeof( xnode_file_path ) );
            xnode_file_total = total_bytes;
            xnode_file_offset = 0;
            xnode_file_next_status = 4096;
            xnode_file_active = true;
            return( true );
        }

        bool xnode_handle_file_stream_frame( const char *frame ) {
            const char *offset_start = frame ? frame + 2 : NULL;
            const char *first = offset_start ? strchr( offset_start, ':' ) : NULL;
            const char *second = first ? strchr( first + 1, ':' ) : NULL;
            char offset_text[ 16 ] = { 0 };
            char total_text[ 16 ] = { 0 };
            char detail[ 160 ] = { 0 };
            uint8_t *decoded_data = NULL;
            size_t decoded_len = 0;
            struct stat st;

            if ( !frame || strncmp( frame, "T:", 2 ) != 0 ) {
                return( false );
            }
            if ( !first || !second || second[ 1 ] == '\0' ) {
                xnode_send_status_event( "tile-write-error", "stream-frame-invalid", XNODE_OFFLINE_TILE_ROOT );
                xnode_set_basemap_storage_busy( false );
                xnode_reset_file_transfer();
                return( true );
            }

            const size_t offset_len = min( (size_t)( first - offset_start ), sizeof( offset_text ) - 1 );
            const size_t total_len = min( (size_t)( second - first - 1 ), sizeof( total_text ) - 1 );
            memcpy( offset_text, offset_start, offset_len );
            memcpy( total_text, first + 1, total_len );

            const size_t offset = (size_t)strtoul( offset_text, NULL, 10 );
            const size_t total_bytes = (size_t)strtoul( total_text, NULL, 10 );
            const char *encoded = second + 1;

            if ( !xnode_file_active || !xnode_file_path[ 0 ] || total_bytes != xnode_file_total ) {
                snprintf(
                    detail,
                    sizeof( detail ),
                    "stream-offset-mismatch %s have=%u want=%u",
                    xnode_file_path,
                    (unsigned)xnode_file_offset,
                    (unsigned)offset
                );
                xnode_send_status_event( "tile-write-error", detail, XNODE_OFFLINE_TILE_ROOT, xnode_file_offset, xnode_file_total );
                xnode_set_basemap_storage_busy( false );
                xnode_reset_file_transfer();
                return( true );
            }

            if ( !xnode_base64url_decode_bytes( encoded, &decoded_data, &decoded_len ) || !decoded_data || decoded_len == 0 ) {
                xnode_send_status_event( "tile-write-error", "stream-decode-error", XNODE_OFFLINE_TILE_ROOT, xnode_file_offset, xnode_file_total );
                xnode_set_basemap_storage_busy( false );
                xnode_reset_file_transfer();
                if ( decoded_data ) {
                    free( decoded_data );
                }
                return( true );
            }

            if ( offset < xnode_file_offset && offset + decoded_len <= xnode_file_offset ) {
                free( decoded_data );
                xnode_send_status_event( "tile-progress", xnode_file_path, XNODE_OFFLINE_TILE_ROOT, xnode_file_offset, xnode_file_total );
                return( true );
            }

            if ( offset != xnode_file_offset ) {
                free( decoded_data );
                snprintf(
                    detail,
                    sizeof( detail ),
                    "stream-offset-mismatch %s have=%u want=%u",
                    xnode_file_path,
                    (unsigned)xnode_file_offset,
                    (unsigned)offset
                );
                xnode_send_status_event( "tile-write-error", detail, XNODE_OFFLINE_TILE_ROOT, xnode_file_offset, xnode_file_total );
                xnode_set_basemap_storage_busy( false );
                xnode_reset_file_transfer();
                return( true );
            }

            if ( !xnode_write_file_chunk( xnode_file_path, decoded_data, decoded_len, offset > 0, offset, detail, sizeof( detail ) ) ) {
                free( decoded_data );
                if ( detail[ 0 ] == '\0' ) {
                    snprintf( detail, sizeof( detail ), "%s errno=%d", xnode_file_path, errno );
                }
                xnode_send_status_event( "tile-write-error", detail, XNODE_OFFLINE_TILE_ROOT, xnode_file_offset, xnode_file_total );
                xnode_set_basemap_storage_busy( false );
                xnode_reset_file_transfer();
                return( true );
            }
            free( decoded_data );

            xnode_file_offset += decoded_len;
            if ( xnode_file_offset >= xnode_file_total ) {
                const bool stat_ok = stat( xnode_file_path, &st ) == 0;
                const size_t stored_size = stat_ok ? (size_t)st.st_size : 0;

                if ( !stat_ok || stored_size != xnode_file_total ) {
                    snprintf(
                        detail,
                        sizeof( detail ),
                        "size-mismatch %s have=%u want=%u",
                        xnode_file_path,
                        (unsigned)stored_size,
                        (unsigned)xnode_file_total
                    );
                    xnode_send_status_event( "tile-write-error", detail, XNODE_OFFLINE_TILE_ROOT, stored_size, xnode_file_total );
                    xnode_set_basemap_storage_busy( false );
                    xnode_reset_file_transfer();
                    return( true );
                }
                uint32_t hash_value = 0;
                char hash_text[ 12 ] = { 0 };

                snprintf( detail, sizeof( detail ), "%s bytes=%u", xnode_file_path, (unsigned)xnode_file_total );
                if ( xnode_file_hash32( xnode_file_path, &hash_value, NULL ) ) {
                    xnode_hash32_hex( hash_value, hash_text, sizeof( hash_text ) );
                }
                xnode_send_status_event( "tile-stored", detail, XNODE_OFFLINE_TILE_ROOT, xnode_file_offset, xnode_file_total, xnode_file_path, hash_text );
                if ( !strcmp( xnode_file_path, XNODE_CURRENT_TILE_PATH ) ) {
                    remove( XNODE_SEED_TILE_PATH );
                }
                xnode_reset_file_transfer();
                return( true );
            }

            if ( xnode_file_offset >= xnode_file_next_status ) {
                xnode_send_status_event( "tile-progress", xnode_file_path, XNODE_OFFLINE_TILE_ROOT, xnode_file_offset, xnode_file_total );
                while ( xnode_file_next_status <= xnode_file_offset ) {
                    xnode_file_next_status += 4096;
                }
            }
            return( true );
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
            SpiRamJsonDocument payload( 3072 );
            JsonArray capabilities = payload.createNestedArray( "capabilities" );
            const bool meshcore = mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE;
            xnode_fill_protocol_payload(payload.as<JsonObject>());

            payload[ "deviceName" ] = device_get_name();
            payload[ "protocolVersion" ] = 1;
            payload[ "firmware" ] = "My-TTGO-Watch-Gen3";
            payload[ "hardware" ] = HARDWARE_NAME;
            payload[ "meshReady" ] = meshtastic_service_is_ready();
            payload[ "meshStatus" ] = meshtastic_service_get_status();
            if (meshcore) {
                xnode_fill_meshcore_user_payload(payload.createNestedObject("meshcoreUser"));
                xnode_fill_meshcore_radio_payload(payload.createNestedObject("meshcoreRadioActive"), true);
                xnode_fill_meshcore_radio_payload(payload.createNestedObject("meshcoreRadioSelected"), false);
            } else {
                payload["nodeId"] = meshtastic_service_get_node_id();
                xnode_fill_meshtastic_user_payload(payload.createNestedObject("meshtasticUser"));
            }
            payload[ "watchUnitId" ] = xnode_watch_unit_id;
            payload[ "sosToUnitId" ] = xnode_sos_to_unit_id;
            payload[ "hasLocation" ] = xnode_has_location;
            if ( xnode_has_location ) {
                payload[ "lat" ] = xnode_last_lat;
                payload[ "lon" ] = xnode_last_lon;
            }
            capabilities.add( "sync" );
            capabilities.add( "location" );
            capabilities.add( xnode_protocol_id(mesh_protocol_get_active()) );
            capabilities.add( "meshProtocolSelection" );
            capabilities.add( "meshSend" );
            capabilities.add( "meshChannels" );
            capabilities.add( "basemap" );
            capabilities.add( "mapOverlay" );
            capabilities.add( "newsNotifications" );
            capabilities.add( "manualSos" );
            capabilities.add( meshcore ? "meshcoreNodeConfig" : "meshtasticNodeConfig" );
            if (meshcore) capabilities.add("meshcoreRadioConfig");
            capabilities.add( "ble" );

            return( !payload.overflowed() && xnode_send_event( "helloAck", payload ) );
        }

        bool xnode_apply_location_payload( JsonObjectConst payload, bool notify_user ) {
            const double lat = payload[ "lat" ].isNull() ? ( payload[ "latitude" ] | 999.0 ) : ( payload[ "lat" ] | 999.0 );
            const double lon = payload[ "lon" ].isNull() ? ( payload[ "longitude" ] | 999.0 ) : ( payload[ "lon" ] | 999.0 );
            const char *label = payload[ "label" ].isNull() ? ( payload[ "sharedBy" ] | xnode_last_host_name ) : ( payload[ "label" ] | xnode_last_host_name );

            if ( lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ) {
                return( false );
            }

            osmmap_set_external_marker( lon, lat, label ? label : "XTOC" );
            xnode_last_lat = lat;
            xnode_last_lon = lon;
            xnode_has_location = true;

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
            const char *color = item[ "color" ] | "";
            const double lat = item[ "lat" ].isNull() ? ( item[ "latitude" ] | 999.0 ) : ( item[ "lat" ] | 999.0 );
            const double lon = item[ "lon" ].isNull() ? ( item[ "longitude" ] | 999.0 ) : ( item[ "lon" ] | 999.0 );
            const uint32_t updated_at = item[ "updatedAt" ].isNull() ? ( item[ "packetAt" ] | 0 ) : ( item[ "updatedAt" ] | 0 );
            const double map_x = item[ "mapX" ].isNull() ? ( item[ "px" ] | 999999.0 ) : ( item[ "mapX" ] | 999999.0 );
            const double map_y = item[ "mapY" ].isNull() ? ( item[ "py" ] | 999999.0 ) : ( item[ "mapY" ] | 999999.0 );
            const bool has_pixel = std::isfinite( map_x ) && std::isfinite( map_y ) && map_x >= 0.0 && map_x < 256.0 && map_y >= 0.0 && map_y < 256.0;
            const int16_t pixel_x = has_pixel ? (int16_t)lround( fmax( 0.0, fmin( 255.0, map_x ) ) ) : 0;
            const int16_t pixel_y = has_pixel ? (int16_t)lround( fmax( 0.0, fmin( 255.0, map_y ) ) ) : 0;

            if ( !key[ 0 ] || !kind[ 0 ] ) {
                return( false );
            }
            if ( lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ) {
                return( false );
            }

            osmmap_upsert_overlay_item( key, kind, lon, lat, label, updated_at, color, has_pixel, pixel_x, pixel_y );
            return( true );
        }

        void xnode_handle_command( DynamicJsonDocument &doc, bool authenticated ) {
            const char *type = doc[ "type" ] | "";
            JsonObjectConst payload = doc[ "payload" ].as<JsonObjectConst>();

            if ( payload[ "deviceName" ].is<const char *>() ) {
                strlcpy( xnode_last_host_name, payload[ "deviceName" ], sizeof( xnode_last_host_name ) );
            }

            if ( strcmp( type, "hello" ) == 0 ) {
                xnode_send_hello_ack();
                return;
            }

            const char *protected_reply = xnode_mesh_mutation_reply_type(type);
            if (protected_reply && !authenticated) {
                StaticJsonDocument<256> reply;
                reply["requestId"] = doc["requestId"] | "";
                reply["ok"] = false;
                reply["error"] = "pairing-required";
                reply["protocol"] = xnode_protocol_id(mesh_protocol_get_active());
                xnode_send_event(protected_reply, reply);
                return;
            }

            if (!strcmp(type, "getMeshProtocol") || !strcmp(type, "setMeshProtocol")) {
                StaticJsonDocument<1024> reply;
                reply["requestId"] = doc["requestId"] | "";
                bool ok = true;
                const char *error = "";
                if (!strcmp(type, "setMeshProtocol")) {
                    mesh_protocol_t requested;
                    // Selection never restarts this device. A separate, explicit on-device
                    // Restart confirmation applies both staged protocol and radio settings.
                    if (payload.containsKey("reboot") && (!payload["reboot"].is<bool>() || payload["reboot"].as<bool>())) {
                        ok = false; error = "restart-on-device-required";
                    } else if (!xnode_parse_protocol(payload["protocol"], requested)) {
                        ok = false; error = "unsupported-protocol";
                    } else if (!mesh_protocol_select(requested)) {
                        ok = false; error = "save-failed";
                    }
                }
                xnode_fill_protocol_payload(reply.to<JsonObject>());
                reply["requestId"] = doc["requestId"] | "";
                reply["meshReady"] = meshtastic_service_is_ready();
                reply["meshStatus"] = meshtastic_service_get_status();
                if (mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE)
                    xnode_fill_meshcore_user_payload(reply.createNestedObject("meshcoreUser"));
                reply["ok"] = ok;
                reply["error"] = error;
                reply["restarted"] = false;
                reply["restartAction"] = "device-radio-restart";
                xnode_send_event("meshProtocolAck", reply);
                return;
            }

            if (!strcmp(type, "meshSend") || !strcmp(type, "selectMeshChannel")) {
                StaticJsonDocument<512> reply;
                reply["requestId"] = doc["requestId"] | "";
                bool ok = false;
                const char *error = "protocol-mismatch";
                if (xnode_payload_matches_active_protocol(payload)) {
                    if (!strcmp(type, "meshSend")) {
                        const char *text = payload["text"] | "";
                        bool has_text = false;
                        for (const char *p = text; *p; ++p) if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') has_text = true;
                        // This command is deliberately group-only. Do not truncate a public
                        // key or interpret a Meshtastic destination as a MeshCore identity.
                        if (payload.containsKey("to") || payload.containsKey("dest") || payload.containsKey("destination") || payload.containsKey("toNode") || payload.containsKey("to_node") || payload.containsKey("nodeId") || payload.containsKey("publicKey")) error = "broadcast-only";
                        else if (payload.containsKey("channel") || payload.containsKey("channelIndex") || payload.containsKey("channelSlot")) error = "select-channel-first";
                        else if (!has_text || strlen(text) > 200) error = "invalid-text";
                        else { ok = meshtastic_service_send_text(text); error = ok ? "" : "send-rejected"; }
                    } else if (!payload["index"].is<unsigned>() || payload["index"].as<unsigned>() >= meshtastic_service_get_channel_count()) {
                        error = "invalid-channel-index";
                    } else { ok = meshtastic_service_set_active_channel(payload["index"].as<unsigned>()); error = ok ? "" : "channel-rejected"; }
                }
                reply["ok"] = ok;
                reply["error"] = error;
                reply["protocol"] = xnode_protocol_id(mesh_protocol_get_active());
                reply["status"] = meshtastic_service_get_status();
                reply["channelIndex"] = meshtastic_service_get_active_channel();
                reply["channelSlot"] = meshtastic_service_get_channel_slot(meshtastic_service_get_active_channel());
                if (!strcmp(type, "meshSend")) { reply["queued"] = ok; reply["delivered"] = false; }
                else reply["activeIndex"] = meshtastic_service_get_active_channel();
                xnode_send_event(!strcmp(type, "meshSend") ? "meshSendAck" : "meshChannelAck", reply);
                return;
            }

            if (!strcmp(type, "getMeshChannels") || !strcmp(type, "setMeshcoreChannel")) {
                SpiRamJsonDocument reply(2048);
                reply["requestId"] = doc["requestId"] | "";
                const bool set_channel = !strcmp(type, "setMeshcoreChannel");
                bool ok = xnode_payload_matches_active_protocol(payload) && (!set_channel || mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE);
                const char *error = ok ? "" : "protocol-mismatch";
                if (ok && set_channel) {
                    uint8_t slot = 0;
                    meshtastic_service_channel_info_t info = {};
                    if (!xnode_parse_meshcore_channel(payload, slot, info)) { ok = false; error = "invalid-channel"; }
                    else if (!meshtastic_service_set_channel_info(slot, &info)) { ok = false; error = "save-failed"; }
                }
                reply["ok"] = ok;
                reply["error"] = error;
                reply["protocol"] = xnode_protocol_id(mesh_protocol_get_active());
                reply["activeIndex"] = meshtastic_service_get_active_channel();
                JsonArray channels = reply.createNestedArray("channels");
                if (ok) for (uint8_t index = 0; index < meshtastic_service_get_channel_count(); ++index) {
                    const int8_t slot = meshtastic_service_get_channel_slot(index);
                    meshtastic_service_channel_info_t info = {};
                    if (slot < 0 || !meshtastic_service_get_channel_info(slot, &info)) continue;
                    JsonObject item = channels.createNestedObject();
                    item["index"] = index; item["slot"] = slot; item["name"] = info.name;
                    item["keyBytes"] = info.psk_len;
                }
                xnode_send_event("meshChannelsAck", reply);
                return;
            }

            if (!strcmp(type, "setMeshcoreUser")) {
                StaticJsonDocument<512> reply;
                reply["requestId"] = doc["requestId"] | "";
                const char *name = payload["longName"] | "";
                bool ok = mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE;
                const char *error = ok ? "" : "protocol-mismatch";
                if (ok) {
                    if (!xnode_valid_mesh_name(name, 31) || payload.containsKey("shortName") || payload.containsKey("isLicensed") || payload.containsKey("isUnmessageable") ||
                        (payload.containsKey("broadcast") && !payload["broadcast"].is<bool>())) { ok = false; error = "invalid-user"; }
                    else {
                        meshtastic_service_user_info_t info = {};
                        meshtastic_service_get_user_info(&info);
                        strlcpy(info.long_name, name, sizeof(info.long_name));
                        ok = meshtastic_service_set_user_info(&info);
                        if (!ok) error = "save-failed";
                    }
                }
                const bool broadcast = ok && (payload["broadcast"] | false);
                if (broadcast) meshtastic_service_schedule_node_info_broadcast(250);
                // The backend automatically schedules an advertisement after a name save;
                // broadcast:true merely brings that scheduled send forward.
                reply["ok"] = ok; reply["error"] = error; reply["broadcastQueued"] = ok;
                if (mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE) xnode_fill_meshcore_user_payload(reply.createNestedObject("meshcoreUser"));
                xnode_send_event("meshcoreUserAck", reply);
                return;
            }

            if (!strcmp(type, "getMeshcoreRadio") || !strcmp(type, "setMeshcoreRadio")) {
                StaticJsonDocument<768> reply;
                reply["requestId"] = doc["requestId"] | "";
                bool ok = mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE;
                const char *error = ok ? "" : "protocol-mismatch";
                if (ok && !strcmp(type, "setMeshcoreRadio")) {
                    meshcore_service_radio_config_t config = {};
                    // Check JSON types and integer bounds before narrowing them.
                    if (!payload["frequencyMhz"].is<float>() || !payload["bandwidthKhz"].is<float>() ||
                        !payload["spreadingFactor"].is<unsigned>() || payload["spreadingFactor"].as<unsigned>() < 7 || payload["spreadingFactor"].as<unsigned>() > 12 ||
                        !payload["codingRate"].is<unsigned>() || payload["codingRate"].as<unsigned>() < 5 || payload["codingRate"].as<unsigned>() > 8 ||
                        !payload["txPowerDbm"].is<int>() || payload["txPowerDbm"].as<int>() < -9 || payload["txPowerDbm"].as<int>() > 22) {
                        ok = false; error = "invalid-radio-profile";
                    } else {
                        config.frequency_mhz = payload["frequencyMhz"].as<float>(); config.bandwidth_khz = payload["bandwidthKhz"].as<float>();
                        config.spreading_factor = payload["spreadingFactor"].as<unsigned>(); config.coding_rate = payload["codingRate"].as<unsigned>(); config.tx_power_dbm = payload["txPowerDbm"].as<int>();
                        ok = meshcore_service_set_radio_config(&config);
                        if (!ok) error = "invalid-profile-or-save-failed";
                    }
                }
                reply["ok"] = ok; reply["error"] = error; reply["restarted"] = false;
                reply["radioRequiresRestart"] = meshcore_service_radio_reboot_required();
                if (mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE) {
                    xnode_fill_meshcore_radio_payload(reply.createNestedObject("active"), true);
                    xnode_fill_meshcore_radio_payload(reply.createNestedObject("selected"), false);
                }
                xnode_send_event("meshcoreRadioAck", reply);
                return;
            }

            if ( strcmp( type, "setSosConfig" ) == 0 ) {
                StaticJsonDocument< 160 > reply;

                xnode_apply_sos_config_payload( payload );
                xnode_save_persistent_config();
                reply[ "watchUnitId" ] = xnode_watch_unit_id;
                reply[ "sosToUnitId" ] = xnode_sos_to_unit_id;
                xnode_send_event( "sosConfigAck", reply );
                return;
            }

            if ( strcmp( type, "setMeshtasticUser" ) == 0 ) {
                StaticJsonDocument< 384 > reply;
                if (mesh_protocol_get_active() != MESH_PROTOCOL_MESHTASTIC) {
                    reply["ok"] = false; reply["broadcastQueued"] = false;
                    reply["error"] = "protocol-mismatch";
                    xnode_send_event("meshtasticUserAck", reply);
                    return;
                }
                bool broadcast_requested = false;
                const bool saved = xnode_apply_meshtastic_user_payload( payload, &broadcast_requested );
                JsonObject user = reply.createNestedObject( "meshtasticUser" );

                if ( saved && broadcast_requested ) {
                    meshtastic_service_schedule_node_info_broadcast( 250 );
                }

                reply[ "ok" ] = saved;
                reply[ "broadcastQueued" ] = saved && broadcast_requested;
                xnode_fill_meshtastic_user_payload( user );
                xnode_send_event( "meshtasticUserAck", reply );
                return;
            }

            if ( strcmp( type, "syncState" ) == 0 ) {
                StaticJsonDocument< 384 > reply;
                const bool replace = payload[ "replace" ] | false;
                const bool has_location = payload.containsKey( "location" );
                const size_t expected_overlays = payload.containsKey( "overlayCount" )
                    ? ( payload[ "overlayCount" ] | 0 )
                    : ( payload[ "packetCount" ] | 0 );

                if ( has_location ) {
                    JsonObjectConst location = payload[ "location" ].as<JsonObjectConst>();
                    if ( xnode_apply_location_payload( location, false ) ) {
                        xnode_save_persistent_location_if_due( false );
                    }
                    xnode_apply_time_payload( location );
                }
                else if ( replace ) {
                    osmmap_clear_external_marker();
                }
                if ( payload.containsKey( "basemap" ) ) {
                    JsonObjectConst basemap = payload[ "basemap" ].as<JsonObjectConst>();
                    xnode_send_status_event( "profile-staged", basemap[ "name" ] | "", XNODE_OFFLINE_TILE_ROOT );
                }
                if ( replace ) {
                    xnode_overlay_sync_replacing = expected_overlays > 0;
                    xnode_overlay_sync_expected = expected_overlays;
                    xnode_overlay_sync_seen = 0;
                    if ( expected_overlays == 0 ) {
                        osmmap_clear_overlay_items();
                        osmmap_clear_persisted_overlay_items();
                    }
                    else {
                        osmmap_begin_overlay_replace();
                    }
                }
                else {
                    osmmap_cancel_overlay_replace();
                    xnode_overlay_sync_replacing = false;
                    xnode_overlay_sync_expected = 0;
                    xnode_overlay_sync_seen = 0;
                }

                reply[ "packetCount" ] = payload[ "packetCount" ] | 0;
                reply[ "overlayCount" ] = osmmap_overlay_item_count();
                reply[ "meshCount" ] = payload[ "meshCount" ] | 0;
                reply[ "replace" ] = replace;
                reply[ "watchUnitId" ] = xnode_watch_unit_id;
                reply[ "sosToUnitId" ] = xnode_sos_to_unit_id;
                xnode_send_event( "syncAck", reply );
                return;
            }

            if ( strcmp( type, "overlayBatch" ) == 0 ) {
                StaticJsonDocument< 256 > reply;
                JsonArrayConst items = payload[ "items" ].as<JsonArrayConst>();
                size_t applied = 0;
                size_t seen = 0;

                if ( !items.isNull() ) {
                    for ( JsonVariantConst raw_item : items ) {
                        JsonObjectConst item = raw_item.as<JsonObjectConst>();

                        if ( item.isNull() ) {
                            continue;
                        }
                        seen++;
                        if ( xnode_apply_overlay_item_payload( item ) ) {
                            applied++;
                        }
                    }
                }
                if ( xnode_overlay_sync_replacing ) {
                    xnode_overlay_sync_seen += seen;
                    if ( xnode_overlay_sync_seen >= xnode_overlay_sync_expected ) {
                        osmmap_commit_overlay_replace();
                        osmmap_save_overlay_items();
                        xnode_overlay_sync_replacing = false;
                        xnode_overlay_sync_expected = 0;
                        xnode_overlay_sync_seen = 0;
                    }
                }
                else {
                    osmmap_save_overlay_items();
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
                size_t seen = 0;

                if ( !packets.isNull() ) {
                    for ( JsonVariantConst raw_packet : packets ) {
                        JsonObjectConst packet = raw_packet.as<JsonObjectConst>();

                        if ( packet.isNull() ) {
                            continue;
                        }
                        seen++;

                        const char *key = packet[ "key" ] | "";
                        const char *kind = xnode_overlay_kind_for_template( packet[ "templateId" ] | 0 );
                        const char *label = packet[ "summary" ] | ( kind ? kind : "" );
                        const char *color = packet[ "color" ] | "";
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
                        osmmap_upsert_overlay_item( overlay_key, kind, lon, lat, label, packet_at, color );
                        applied++;
                    }
                }

                if ( xnode_overlay_sync_replacing ) {
                    xnode_overlay_sync_seen += seen;
                    if ( xnode_overlay_sync_seen >= xnode_overlay_sync_expected ) {
                        osmmap_commit_overlay_replace();
                        osmmap_save_overlay_items();
                        xnode_overlay_sync_replacing = false;
                        xnode_overlay_sync_expected = 0;
                        xnode_overlay_sync_seen = 0;
                    }
                }
                else {
                    osmmap_save_overlay_items();
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

            if ( strcmp( type, "meshtasticRx" ) == 0 || strcmp(type, "meshRx") == 0 ) {
                if ((!strcmp(type, "meshtasticRx") && mesh_protocol_get_active() != MESH_PROTOCOL_MESHTASTIC) ||
                    (!strcmp(type, "meshRx") && !xnode_payload_matches_active_protocol(payload))) return;
                const char *text = payload[ "text" ] | "";
                const char *from = payload[ "from" ] | mesh_protocol_name(mesh_protocol_get_active());

                if ( text[ 0 ] ) {
                    xnode_queue_notification( from, text );
                }
                return;
            }

            if ( strcmp( type, "newsItem" ) == 0 ) {
                StaticJsonDocument< 256 > reply;
                const char *source = payload[ "source" ].isNull() ? ( payload[ "src" ] | xnode_last_host_name ) : ( payload[ "source" ] | xnode_last_host_name );
                const char *title = payload[ "title" ] | "News";
                const char *body = payload[ "body" ].isNull() ? ( payload[ "text" ] | "" ) : ( payload[ "body" ] | "" );
                double ts_value = payload[ "ts" ] | 0.0;

                if ( ts_value > 2000000000.0 ) {
                    ts_value /= 1000.0;
                }

                const bool stored = xnode_notifications_push( source, title, body, (uint32_t)ts_value );
                reply[ "id" ] = payload[ "id" ] | "";
                reply[ "stored" ] = stored;
                reply[ "enabled" ] = xnode_notifications_get_enabled();
                reply[ "count" ] = xnode_notifications_get_count();
                xnode_send_event( "newsItemAck", reply );
                return;
            }

            if ( strcmp( type, "setNewsNotifications" ) == 0 ) {
                StaticJsonDocument< 128 > reply;
                const bool enabled = payload[ "enabled" ] | true;

                xnode_notifications_set_enabled( enabled );
                reply[ "enabled" ] = xnode_notifications_get_enabled();
                reply[ "count" ] = xnode_notifications_get_count();
                xnode_send_event( "newsNotificationsConfig", reply );
                return;
            }

            if ( strcmp( type, "location" ) == 0 ) {
                StaticJsonDocument< 256 > reply;
                const double lat = payload[ "lat" ].isNull() ? ( payload[ "latitude" ] | 0.0 ) : ( payload[ "lat" ] | 0.0 );
                const double lon = payload[ "lon" ].isNull() ? ( payload[ "longitude" ] | 0.0 ) : ( payload[ "lon" ] | 0.0 );
                const char *label = payload[ "label" ].isNull() ? ( payload[ "sharedBy" ] | xnode_last_host_name ) : ( payload[ "label" ] | xnode_last_host_name );
                const bool time_updated = xnode_apply_time_payload( payload );

                if ( xnode_apply_location_payload( payload, false ) ) {
                    xnode_save_persistent_location_if_due( true );
                    reply[ "lat" ] = lat;
                    reply[ "lon" ] = lon;
                    reply[ "label" ] = label;
                    reply[ "timeUpdated" ] = time_updated;
                    xnode_send_event( "locationAck", reply );
                }
                return;
            }

            if ( strcmp( type, "mapTileBegin" ) == 0 ) {
                const char *filepath = payload[ "path" ] | "";
                const size_t total_bytes = payload[ "totalBytes" ] | 0;
                char status_name[ 160 ] = { 0 };

                if ( xnode_begin_file_transfer( filepath, total_bytes, status_name, sizeof( status_name ) ) ) {
                    xnode_send_status_event( "tile-ready", xnode_file_path, XNODE_OFFLINE_TILE_ROOT, 0, total_bytes, xnode_file_path );
                }
                else {
                    if ( status_name[ 0 ] == '\0' ) {
                        snprintf( status_name, sizeof( status_name ), "begin-failed %s", filepath );
                    }
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_set_basemap_storage_busy( false );
                    xnode_reset_file_transfer();
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
                char resolved_path[ sizeof( xnode_file_path ) ] = { 0 };
                uint8_t *decoded_data = NULL;
                size_t decoded_len = 0;
                struct stat st;

                if ( !filepath[ 0 ] ) {
                    snprintf( status_name, sizeof( status_name ), "path-missing" );
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_set_basemap_storage_busy( false );
                    return;
                }

                if ( !xnode_resolve_watch_path( filepath, resolved_path, sizeof( resolved_path ), status_name, sizeof( status_name ) ) ) {
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_set_basemap_storage_busy( false );
                    return;
                }

                if ( !encoded[ 0 ] ) {
                    snprintf( status_name, sizeof( status_name ), "payload-missing %s", resolved_path );
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_set_basemap_storage_busy( false );
                    return;
                }

                if ( !xnode_base64url_decode_bytes( encoded, &decoded_data, &decoded_len ) ) {
                    snprintf( status_name, sizeof( status_name ), "payload-decode-error %s len=%u", resolved_path, (unsigned)strlen( encoded ) );
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_set_basemap_storage_busy( false );
                    return;
                }

                if ( !append && offset == 0 ) {
                    xnode_set_basemap_storage_busy( true );
                }

                if ( xnode_write_file_chunk( resolved_path, decoded_data, decoded_len, append, offset, status_name, sizeof( status_name ) ) ) {
                    free( decoded_data );
                    if ( total_bytes > 0 && ( offset + decoded_len ) >= total_bytes ) {
                        const bool stat_ok = stat( resolved_path, &st ) == 0;
                        const size_t stored_size = stat_ok ? (size_t)st.st_size : 0;

                        if ( !stat_ok || stored_size != total_bytes ) {
                            snprintf(
                                status_name,
                                sizeof( status_name ),
                                "size-mismatch %s have=%u want=%u",
                                resolved_path,
                                (unsigned)stored_size,
                                (unsigned)total_bytes
                            );
                            xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                            xnode_set_basemap_storage_busy( false );
                            return;
                        }
                        uint32_t hash_value = 0;
                        char hash_text[ 12 ] = { 0 };

                        snprintf( status_name, sizeof( status_name ), "%s bytes=%u", resolved_path, (unsigned)total_bytes );
                        if ( xnode_file_hash32( resolved_path, &hash_value, NULL ) ) {
                            xnode_hash32_hex( hash_value, hash_text, sizeof( hash_text ) );
                        }
                        xnode_send_status_event( "tile-stored", status_name, XNODE_OFFLINE_TILE_ROOT, stored_size, total_bytes, resolved_path, hash_text );
                        if ( !strcmp( resolved_path, XNODE_CURRENT_TILE_PATH ) ) {
                            remove( XNODE_SEED_TILE_PATH );
                        }
                    }
                    return;
                }
                free( decoded_data );

                if ( status_name[ 0 ] == '\0' ) {
                    snprintf( status_name, sizeof( status_name ), "%s errno=%d", resolved_path[ 0 ] ? resolved_path : filepath, errno );
                }
                xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                xnode_set_basemap_storage_busy( false );
                return;
            }

            if ( strcmp( type, "clearBasemap" ) == 0 ) {
                char status_name[ 160 ] = { 0 };

                xnode_reset_file_transfer();
                xnode_set_basemap_storage_busy( true );
                if ( !xnode_ensure_watch_parent_dirs( XNODE_CURRENT_TILE_PATH, status_name, sizeof( status_name ) ) ) {
                    xnode_send_status_event( "tile-write-error", status_name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_set_basemap_storage_busy( false );
                    return;
                }
                osmmap_prepare_watch_basemap_file_replace( XNODE_CURRENT_TILE_PATH );
                remove( XNODE_CURRENT_TILE_PATH );
                remove( XNODE_SEED_TILE_PATH );
                osmmap_clear_overlay_items();
                osmmap_clear_persisted_overlay_items();
                osmmap_cancel_overlay_replace();
                xnode_overlay_sync_replacing = false;
                xnode_overlay_sync_expected = 0;
                xnode_overlay_sync_seen = 0;
                xnode_send_status_event( "tile-cleared", XNODE_CURRENT_TILE_PATH, XNODE_OFFLINE_TILE_ROOT, 0, 0, XNODE_CURRENT_TILE_PATH );
                return;
            }

            if ( strcmp( type, "installBasemap" ) == 0 ) {
                JsonObjectConst manifest = payload[ "manifest" ].as<JsonObjectConst>();
                const char *name = manifest[ "name" ] | "Watch Basemap";
                JsonObjectConst center = manifest[ "center" ].as<JsonObjectConst>();
                JsonObjectConst projection = manifest[ "projection" ].as<JsonObjectConst>();
                const double center_lat = center[ "lat" ] | 999.0;
                const double center_lon = center[ "lon" ] | 999.0;
                const uint32_t center_zoom = center[ "zoom" ] | ( manifest[ "minZoom" ] | 10 );
                const double projection_zoom_raw = projection[ "zoom" ] | -1.0;
                const uint32_t projection_zoom = projection_zoom_raw >= 0.0 && projection_zoom_raw <= 22.0
                    ? (uint32_t)lround( projection_zoom_raw )
                    : 0xffffffffUL;
                osmmap_config_t config;
                char body[ 160 ];
                char active_path[ 160 ] = { 0 };
                char active_hash[ 12 ] = { 0 };
                size_t active_bytes = 0;
                uint32_t active_hash_value = 0;

                config.load();
                strlcpy( config.osmmap, XNODE_OFFLINE_MAP_NAME, sizeof( config.osmmap ) );
                config.save();
                if ( !osmmap_apply_watch_basemap( XNODE_OFFLINE_MAP_NAME, center_lon, center_lat, center_zoom, projection_zoom ) ) {
                    snprintf( body, sizeof( body ), "profile failed: %s (%s)", name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_queue_notification( "Basemap", body );
                    xnode_send_status_event( "profile-error", name, XNODE_OFFLINE_TILE_ROOT );
                    xnode_set_basemap_storage_busy( false );
                    return;
                }
                xnode_set_basemap_storage_busy( false );
                osmmap_get_watch_basemap_info( active_path, sizeof( active_path ), &active_bytes );
                if ( active_path[ 0 ] && xnode_file_hash32( active_path, &active_hash_value, NULL ) ) {
                    xnode_hash32_hex( active_hash_value, active_hash, sizeof( active_hash ) );
                }

                snprintf( body, sizeof( body ), "profile active: %s (%s %u bytes)", name, active_path[ 0 ] ? active_path : "unknown", (unsigned)active_bytes );
                xnode_queue_notification( "Basemap", body );
                xnode_send_status_event( "profile-active", name, XNODE_OFFLINE_TILE_ROOT, active_bytes, active_bytes, active_path, active_hash );
                xnode_send_request_sync( "basemap-active" );
                return;
            }
        }

        void xnode_handle_frame( const xnode_rx_frame_t &incoming ) {
            if (incoming.connection_epoch != xnode_connection_epoch.load()) { xnode_reset_rx(); return; }
            const char *frame = incoming.text;
            if ( frame && strncmp( frame, "T:", 2 ) == 0 ) {
                xnode_handle_file_stream_frame( frame );
                return;
            }

            const char *first = strchr( frame, ':' );
            const char *second = first ? strchr( first + 1, ':' ) : NULL;
            const char *third = second ? strchr( second + 1, ':' ) : NULL;

            if ( !first || !second || !third ) {
                return;
            }

            char id[ sizeof( xnode_rx_id ) ];
            const size_t id_len = (size_t)( first - frame );
            if (id_len == 0 || id_len >= sizeof(id)) { xnode_reset_rx(); return; }
            memcpy( id, frame, id_len );
            id[ id_len ] = '\0';

            uint16_t index = 0, total = 0;
            const char *chunk = third + 1;

            if ( !xnode_parse_frame_number(first + 1, second, index) ||
                 !xnode_parse_frame_number(second + 1, third, total) || index > total || !chunk[ 0 ] ) {
                xnode_reset_rx();
                return;
            }

            if ( index == 1 || strcmp( id, xnode_rx_id ) != 0 || total != xnode_rx_total ) {
                xnode_reset_rx();
                strlcpy( xnode_rx_id, id, sizeof( xnode_rx_id ) );
                xnode_rx_total = total;
                xnode_rx_connection_epoch = incoming.connection_epoch;
                xnode_rx_connection_handle = incoming.connection_handle;
                xnode_rx_authenticated = incoming.authenticated;
                xnode_rx_encoded.reserve( min( (size_t)total * XNODE_FRAME_CHUNK, (size_t)XNODE_MAX_ENCODED ) );
            }

            if ( strcmp( id, xnode_rx_id ) != 0 || total != xnode_rx_total || index != ( xnode_rx_index + 1 ) ||
                 incoming.connection_epoch != xnode_rx_connection_epoch ||
                 incoming.connection_handle != xnode_rx_connection_handle ||
                 incoming.authenticated != xnode_rx_authenticated ) {
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
                    // Authentication belongs to every fragment's actual connection,
                    // never to a global "connected" UI/event flag.
                    xnode_handle_command( doc, xnode_rx_authenticated && incoming.connection_epoch == xnode_connection_epoch.load() );
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
                    xnode_handle_frame( frame );
                }
            }
        }

        class XnodeCallbacks: public NimBLECharacteristicCallbacks {
            void onWrite( NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc ) override {
                const std::string value = pCharacteristic->getValue();
                // Input can contain channel keys. Never retain it as a readable
                // characteristic value after copying the one incoming frame.
                pCharacteristic->setValue((const uint8_t *)"", 0);
                if (!desc) return;
                xnode_rx_frame_t frame = {};

                if ( !value.empty() && value.size() < sizeof(frame.text) && value.find('\0') == std::string::npos ) {
                    frame.connection_epoch = xnode_connection_epoch.load();
                    frame.connection_handle = desc->conn_handle;
                    frame.authenticated = desc->sec_state.encrypted && desc->sec_state.authenticated;
                    strlcpy( frame.text, value.c_str(), sizeof( frame.text ) );
                    if ( xnode_rx_queue ) {
                        xQueueSend( xnode_rx_queue, &frame, pdMS_TO_TICKS( 20 ) );
                    }
                }
            };
        };

        XnodeCallbacks xnode_callbacks;

        class XnodeSecureCallbacks: public XnodeCallbacks {
            void onRead(NimBLECharacteristic *characteristic, ble_gap_conn_desc *desc) override {
                const uint8_t authenticated = desc && desc->sec_state.encrypted && desc->sec_state.authenticated ? 1 : 0;
                characteristic->setValue(&authenticated, 1);
            }
        };
        XnodeSecureCallbacks xnode_secure_callbacks;

        bool xnode_gpsctl_event_cb( EventBits_t event, void *arg ) {
            gps_data_t *gps_data = (gps_data_t *)arg;

            if ( !gps_data || !gps_data->valid_location ) {
                return( true );
            }
            switch ( event ) {
                case GPSCTL_SET_APP_LOCATION:
                case GPSCTL_UPDATE_LOCATION:
                    ::xnode_send_location_update(
                        gps_data->lat,
                        gps_data->lon,
                        gps_data->gps_source == GPS_SOURCE_GPS ? "GPS" : gpsctl_get_source_str( gps_data->gps_source )
                    );
                    break;
            }
            return( true );
        }
    }

    const char *xnode_ble_service_uuid( void ) {
        return( XNODE_SERVICE_UUID );
    }

    void xnode_on_disconnect(void) {
        xnode_connection_epoch.fetch_add(1);
    }

    void xnode_setup( void ) {
        NimBLEServer *pServer = blectl_get_ble_server();
        NimBLEAdvertising *pAdvertising = blectl_get_ble_advertising();
        NimBLEService *pXnodeService = pServer->createService( NimBLEUUID( XNODE_SERVICE_UUID ) );

        xnode_load_persistent_config();
        xnode_seed_default_basemap_tile();
        gpsctl_register_cb( GPSCTL_SET_APP_LOCATION | GPSCTL_UPDATE_LOCATION, xnode_gpsctl_event_cb, "xnode gps" );

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
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        pXnodeRXCharacteristic->setCallbacks( &xnode_callbacks );

        // Access triggers the existing display/passkey pairing flow. Key-bearing
        // commands use this protected writer, never the legacy cleartext RX.
        NimBLECharacteristic *secure = pXnodeService->createCharacteristic(
            NimBLEUUID(XNODE_CHARACTERISTIC_UUID_SECURE),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN |
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN
        );
        secure->setCallbacks(&xnode_secure_callbacks);

        pXnodeService->start();
        pAdvertising->addServiceUUID( pXnodeService->getUUID() );
    }

    bool xnode_send_meshtastic_rx( const char *from, const char *text ) {
        StaticJsonDocument< 512 > payload;

        payload[ "from" ] = from ? from : mesh_protocol_name(mesh_protocol_get_active());
        payload[ "text" ] = text ? text : "";
        payload[ "protocol" ] = xnode_protocol_id(mesh_protocol_get_active());
        payload[ "mode" ] = mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE ? "group-broadcast" : "channel-mesh";
        payload[ "ts" ] = (uint32_t)( millis() / 1000 );
        return( xnode_send_event( mesh_protocol_get_active() == MESH_PROTOCOL_MESHCORE ? "meshRx" : "meshtasticRx", payload ) );
    }

    bool xnode_send_location_update( double lat, double lon, const char *label ) {
        // Only this device's GPS/local location may update the emergency position.
        if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
        StaticJsonDocument< 320 > payload;

        xnode_last_lat = lat;
        xnode_last_lon = lon;
        xnode_has_location = true;
        xnode_save_persistent_location_if_due( false );
        payload[ "lat" ] = lat;
        payload[ "lon" ] = lon;
        payload[ "label" ] = label ? label : mesh_protocol_name(mesh_protocol_get_active());
        payload[ "ts" ] = (uint32_t)( millis() / 1000 );
        return( xnode_send_event( "location", payload ) );
    }

    bool xnode_send_peer_location( double lat, double lon, const char *label, const char *public_key ) {
        if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
        if (public_key) {
            if (mesh_protocol_get_active() != MESH_PROTOCOL_MESHCORE || strlen(public_key) != 64) return false;
            for (const char *p = public_key; *p; ++p) if (xnode_hex_digit(*p) < 0) return false;
        }
        StaticJsonDocument< 512 > payload;

        // A radio peer's position must never become our SOS/check-in position,
        // including in older hosts that interpret the legacy location event as us.
        payload[ "lat" ] = lat;
        payload[ "lon" ] = lon;
        payload[ "label" ] = label ? label : mesh_protocol_name(mesh_protocol_get_active());
        payload[ "ts" ] = (uint32_t)( millis() / 1000 );
        payload[ "source" ] = "mesh-peer";
        payload[ "protocol" ] = xnode_protocol_id(mesh_protocol_get_active());
        if (public_key) {
            payload[ "publicKey" ] = public_key;
            payload[ "idType" ] = "ed25519-public-key";
        }
        return( xnode_send_event( "meshPeerLocation", payload ) );
    }

    bool xnode_send_manual_sos( void ) {
        char packet[ 160 ] = { 0 };
        char body[ 128 ] = { 0 };

        if ( xnode_watch_unit_id == 0 ) {
            xnode_queue_notification( "Manual SOS", "Set the watch Unit ID in XTOC/XCOM first." );
            return( false );
        }
        if ( !xnode_has_location ) {
            xnode_queue_notification( "Manual SOS", "Set the watch location before sending SOS." );
            return( false );
        }
        if ( !meshtastic_service_is_ready() ) {
            snprintf( body, sizeof( body ), "%s not ready: %s", mesh_protocol_name(mesh_protocol_get_active()), meshtastic_service_get_status() );
            xnode_queue_notification( "Manual SOS", body );
            return( false );
        }
        if ( !xnode_make_manual_sos_packet( packet, sizeof( packet ) ) ) {
            xnode_queue_notification( "Manual SOS", "Could not build SITREP packet." );
            return( false );
        }
        if ( !meshtastic_service_send_text( packet ) ) {
            snprintf( body, sizeof( body ), "Mesh send failed: %s", meshtastic_service_get_status() );
            xnode_queue_notification( "Manual SOS", body );
            return( false );
        }

        snprintf( body, sizeof( body ), "Queued U%u P1 HELP for U%u. Delivery unconfirmed.", (unsigned)xnode_watch_unit_id, (unsigned)xnode_sos_to_unit_id );
        xnode_queue_notification( "Manual SOS", body );
        return( true );
    }

    bool xnode_send_manual_checkin( void ) {
        char packet[ 160 ] = { 0 };
        char body[ 128 ] = { 0 };

        if ( xnode_watch_unit_id == 0 ) {
            xnode_queue_notification( "CheckIn", "Set the watch Unit ID in XTOC/XCOM first." );
            return( false );
        }
        if ( !xnode_has_location ) {
            xnode_queue_notification( "CheckIn", "Set the watch location before checking in." );
            return( false );
        }
        if ( !meshtastic_service_is_ready() ) {
            snprintf( body, sizeof( body ), "%s not ready: %s", mesh_protocol_name(mesh_protocol_get_active()), meshtastic_service_get_status() );
            xnode_queue_notification( "CheckIn", body );
            return( false );
        }
        if ( !xnode_make_manual_checkin_packet( packet, sizeof( packet ) ) ) {
            xnode_queue_notification( "CheckIn", "Could not build CHECKIN/LOC packet." );
            return( false );
        }
        if ( !meshtastic_service_send_text( packet ) ) {
            snprintf( body, sizeof( body ), "Mesh send failed: %s", meshtastic_service_get_status() );
            xnode_queue_notification( "CheckIn", body );
            return( false );
        }

        snprintf( body, sizeof( body ), "Queued U%u OK check-in. Delivery unconfirmed.", (unsigned)xnode_watch_unit_id );
        xnode_queue_notification( "CheckIn", body );
        return( true );
    }

#else

    void xnode_on_disconnect(void) {}

    void xnode_setup( void ) {
    }

    const char *xnode_ble_service_uuid( void ) {
        return( "7f35b8a0-8d1c-4f8b-b8d5-1f1f0c0d0001" );
    }

    bool xnode_send_meshtastic_rx( const char *from, const char *text ) {
        return( false );
    }

    bool xnode_send_location_update( double lat, double lon, const char *label ) {
        return( false );
    }

    bool xnode_send_peer_location( double lat, double lon, const char *label, const char *public_key ) {
        return( false );
    }

    bool xnode_send_manual_sos( void ) {
        return( false );
    }

    bool xnode_send_manual_checkin( void ) {
        return( false );
    }

#endif
