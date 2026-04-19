#include "config.h"
#include "meshtastic_service.h"
#include "meshtastic_channels_config.h"

#if defined( USING_TWATCH_S3 )

    #include <Arduino.h>
    #include <ArduinoJson.h>
    #include <ESP.h>
    #include <LilyGoLib.h>
    #include <RadioLib.h>
    #include <inttypes.h>
    #include <mbedtls/aes.h>
    #include <mbedtls/base64.h>
    #include <stdarg.h>

    #include "app/osmmap/osmmap_app_main.h"
    #include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
    #include "hardware/ble/xnode.h"
    #include "hardware/powermgm.h"

    #if !defined( NATIVE_64BIT )
        #include <SPIFFS.h>
    #endif

    namespace {
        constexpr uint32_t MESHTASTIC_BROADCAST = 0xFFFFFFFFUL;
        constexpr uint8_t MESHTASTIC_TEXT_MESSAGE_APP = 1;
        constexpr uint8_t MESHTASTIC_POSITION_APP = 3;
        constexpr uint8_t MESHTASTIC_SYNC_WORD = 0x2B;
        constexpr uint8_t MESHTASTIC_HOP_RELIABLE = 3;
        constexpr uint8_t MESHTASTIC_FLAG_HOP_START_SHIFT = 5;
        constexpr uint8_t MESHTASTIC_FLAG_HOP_START_MASK = 0xE0;
        constexpr size_t MESHTASTIC_MAX_PACKET_LEN = 255;
        constexpr size_t MESHTASTIC_MAX_TEXT_LEN = 200;
        constexpr float MESHTASTIC_BW_KHZ = 250.0f;
        constexpr uint8_t MESHTASTIC_SF = 11;
        constexpr uint8_t MESHTASTIC_CR = 5;
        constexpr uint16_t MESHTASTIC_PREAMBLE = 16;
        constexpr int8_t MESHTASTIC_TX_POWER = 22;
        constexpr float MESHTASTIC_TCXO_VOLTAGE = 3.0f;
        constexpr const char *MESHTASTIC_DEFAULT_PRIMARY_CHANNEL = "LongFast";
        constexpr const char *MESHTASTIC_DEFAULT_PRIMARY_PSK = "AQ==";
        constexpr uint8_t MESHTASTIC_PRIMARY_CHANNEL_SLOT = 0;
        constexpr size_t MESHTASTIC_MAX_RUNTIME_PSK_LEN = 32;
        constexpr uint8_t meshtastic_default_psk[ 16 ] = {
            0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
            0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
        };

        typedef struct __attribute__((packed)) {
            uint32_t to;
            uint32_t from;
            uint32_t id;
            uint8_t flags;
            uint8_t channel;
            uint8_t next_hop;
            uint8_t relay_node;
        } meshtastic_packet_header_t;

        struct meshtastic_decoded_text_t {
            bool valid = false;
            uint32_t portnum = 0;
            uint32_t dest = 0;
            uint32_t source = 0;
            size_t text_len = 0;
            char text[ MESHTASTIC_MAX_TEXT_LEN + 1 ] = { 0 };
        };

        struct meshtastic_decoded_data_t {
            bool valid = false;
            uint32_t portnum = 0;
            uint32_t dest = 0;
            uint32_t source = 0;
            size_t payload_len = 0;
            uint8_t payload[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };
        };

        struct meshtastic_decoded_position_t {
            bool valid = false;
            bool has_altitude = false;
            int32_t latitude_i = 0;
            int32_t longitude_i = 0;
            int32_t altitude = 0;
        };

        struct meshtastic_runtime_channel_t {
            bool enabled = false;
            char name[ MESHTASTIC_CHANNEL_NAME_LEN ] = { 0 };
            size_t psk_len = 0;
            uint8_t psk[ MESHTASTIC_MAX_RUNTIME_PSK_LEN ] = { 0 };
            uint8_t hash = 0;
        };

        static_assert( sizeof( meshtastic_packet_header_t ) == 16, "unexpected meshtastic header size" );

        SX1262 meshtastic_radio = newModule();

        volatile bool meshtastic_radio_irq = false;
        bool meshtastic_service_started = false;
        bool meshtastic_radio_ready = false;
        bool meshtastic_radio_receiving = false;
        bool meshtastic_tx_active = false;

        uint32_t meshtastic_node_id = 0;
        uint32_t meshtastic_last_peer = 0;
        uint32_t meshtastic_packet_counter = 0;
        int32_t meshtastic_last_rssi = 0;
        float meshtastic_last_snr = 0.0f;

        char meshtastic_status[ 96 ] = "Meshtastic idle";
        char meshtastic_pending_text[ MESHTASTIC_MAX_TEXT_LEN + 1 ] = { 0 };
        char meshtastic_pending_channel_name[ MESHTASTIC_CHANNEL_NAME_LEN ] = "";
        char meshtastic_last_message_sender[ 24 ] = "";
        char meshtastic_last_message_text[ MESHTASTIC_MAX_TEXT_LEN + 1 ] = "";

        meshtastic_channels_config_t meshtastic_channels_config;
        meshtastic_runtime_channel_t meshtastic_channels[ MESHTASTIC_CHANNEL_COUNT ];
        uint8_t meshtastic_enabled_channel_slots[ MESHTASTIC_CHANNEL_COUNT ] = { 0 };
        uint8_t meshtastic_enabled_channel_count = 0;
        uint8_t meshtastic_active_channel_slot = MESHTASTIC_PRIMARY_CHANNEL_SLOT;
        meshtastic_service_text_rx_cb_t meshtastic_text_rx_callback = NULL;

        uint8_t meshtastic_xor_hash( const uint8_t *data, size_t len ) {
            uint8_t hash = 0;

            for ( size_t i = 0; i < len; i++ ) {
                hash ^= data[ i ];
            }
            return( hash );
        }

        uint32_t meshtastic_djb2_hash( const char *str ) {
            uint32_t hash = 5381;

            while ( *str ) {
                hash = ( ( hash << 5 ) + hash ) + (uint8_t)*str;
                str++;
            }
            return( hash );
        }

        size_t meshtastic_expand_psk( const uint8_t *input, size_t input_len, uint8_t *output, size_t max_len ) {
            if ( !output || max_len < sizeof( meshtastic_default_psk ) ) {
                return( 0 );
            }

            if ( input_len == 0 ) {
                return( 0 );
            }

            if ( input_len == 1 ) {
                const uint8_t psk_index = input[ 0 ];

                if ( psk_index == 0 ) {
                    return( 0 );
                }

                memcpy( output, meshtastic_default_psk, sizeof( meshtastic_default_psk ) );
                output[ sizeof( meshtastic_default_psk ) - 1 ] =
                    output[ sizeof( meshtastic_default_psk ) - 1 ] + psk_index - 1;
                return( sizeof( meshtastic_default_psk ) );
            }

            if ( input_len < 16 ) {
                memset( output, 0, 16 );
                memcpy( output, input, input_len );
                return( 16 );
            }

            if ( input_len == 16 || input_len == 32 ) {
                memcpy( output, input, input_len );
                return( input_len );
            }

            if ( input_len < 32 ) {
                memset( output, 0, 32 );
                memcpy( output, input, input_len );
                return( 32 );
            }

            if ( input_len > max_len ) {
                return( 0 );
            }

            memcpy( output, input, input_len );
            return( input_len );
        }

        bool meshtastic_decode_base64_raw( const char *encoded, uint8_t *output, size_t &output_len ) {
            size_t decoded_len = 0;

            output_len = 0;
            if ( !output ) {
                return( false );
            }
            if ( !encoded || encoded[ 0 ] == '\0' ) {
                return( true );
            }

            const int rc = mbedtls_base64_decode(
                output,
                MESHTASTIC_CHANNEL_PSK_B64_LEN,
                &decoded_len,
                (const unsigned char *)encoded,
                strlen( encoded )
            );

            if ( rc != 0 ) {
                return( false );
            }

            output_len = decoded_len;
            return( true );
        }

        bool meshtastic_decode_psk( const char *encoded, uint8_t *output, size_t &output_len ) {
            uint8_t decoded[ MESHTASTIC_CHANNEL_PSK_B64_LEN ] = { 0 };
            size_t decoded_len = 0;

            output_len = 0;
            if ( !encoded || encoded[ 0 ] == '\0' ) {
                return( true );
            }

            if ( !meshtastic_decode_base64_raw( encoded, decoded, decoded_len ) ) {
                return( false );
            }

            output_len = meshtastic_expand_psk( decoded, decoded_len, output, MESHTASTIC_MAX_RUNTIME_PSK_LEN );
            return( decoded_len == 0 || output_len > 0 );
        }

        bool meshtastic_encode_base64_raw( const uint8_t *input, size_t input_len, char *output, size_t output_len ) {
            size_t encoded_len = 0;

            if ( !output || output_len == 0 ) {
                return( false );
            }

            output[ 0 ] = '\0';
            if ( !input || input_len == 0 ) {
                return( true );
            }

            const int rc = mbedtls_base64_encode(
                (unsigned char *)output,
                output_len,
                &encoded_len,
                input,
                input_len
            );

            if ( rc != 0 || encoded_len >= output_len ) {
                return( false );
            }

            output[ encoded_len ] = '\0';
            return( true );
        }

        uint8_t meshtastic_channel_hash( const char *channel_name, const uint8_t *psk, size_t psk_len ) {
            return( meshtastic_xor_hash( (const uint8_t *)channel_name, strlen( channel_name ) ) ^
                    meshtastic_xor_hash( psk, psk_len ) );
        }

        const char *meshtastic_primary_channel_name( void ) {
            if ( meshtastic_channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].enabled &&
                 meshtastic_channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].name[ 0 ] ) {
                return( meshtastic_channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].name );
            }
            return( MESHTASTIC_DEFAULT_PRIMARY_CHANNEL );
        }

        int8_t meshtastic_slot_for_list_index( uint8_t channel_index ) {
            if ( channel_index >= meshtastic_enabled_channel_count ) {
                return( -1 );
            }
            return( meshtastic_enabled_channel_slots[ channel_index ] );
        }

        int8_t meshtastic_list_index_for_slot( uint8_t slot ) {
            for ( uint8_t i = 0; i < meshtastic_enabled_channel_count; i++ ) {
                if ( meshtastic_enabled_channel_slots[ i ] == slot ) {
                    return( i );
                }
            }
            return( -1 );
        }

        const meshtastic_runtime_channel_t *meshtastic_active_channel( void ) {
            if ( meshtastic_active_channel_slot < MESHTASTIC_CHANNEL_COUNT &&
                 meshtastic_channels[ meshtastic_active_channel_slot ].enabled ) {
                return( &meshtastic_channels[ meshtastic_active_channel_slot ] );
            }
            return( &meshtastic_channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ] );
        }

        int8_t meshtastic_find_channel_slot_for_hash( uint8_t channel_hash ) {
            for ( uint8_t i = 0; i < meshtastic_enabled_channel_count; i++ ) {
                const uint8_t slot = meshtastic_enabled_channel_slots[ i ];

                if ( meshtastic_channels[ slot ].enabled && meshtastic_channels[ slot ].hash == channel_hash ) {
                    return( slot );
                }
            }
            return( -1 );
        }

        void meshtastic_load_channels( void ) {
            bool dirty = false;

            #if !defined( NATIVE_64BIT )
                const bool config_exists = SPIFFS.exists( MESHTASTIC_CHANNELS_JSON_CONFIG_FILE );
            #else
                const bool config_exists = true;
            #endif

            meshtastic_channels_config.load();

            if ( !config_exists ) {
                dirty = true;
            }

            if ( !meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].enabled ) {
                meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].enabled = true;
                dirty = true;
            }
            if ( !meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].name[ 0 ] ) {
                strlcpy(
                    meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].name,
                    MESHTASTIC_DEFAULT_PRIMARY_CHANNEL,
                    sizeof( meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].name )
                );
                dirty = true;
            }
            if ( !meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].psk[ 0 ] ) {
                strlcpy(
                    meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].psk,
                    MESHTASTIC_DEFAULT_PRIMARY_PSK,
                    sizeof( meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].psk )
                );
                dirty = true;
            }

            memset( meshtastic_channels, 0, sizeof( meshtastic_channels ) );
            memset( meshtastic_enabled_channel_slots, 0, sizeof( meshtastic_enabled_channel_slots ) );
            meshtastic_enabled_channel_count = 0;

            for ( uint8_t slot = 0; slot < MESHTASTIC_CHANNEL_COUNT; slot++ ) {
                meshtastic_channel_config_entry_t &config_entry = meshtastic_channels_config.channels[ slot ];
                meshtastic_runtime_channel_t &runtime_entry = meshtastic_channels[ slot ];
                size_t psk_len = 0;

                if ( !config_entry.enabled ) {
                    continue;
                }

                if ( !config_entry.name[ 0 ] ) {
                    if ( slot == MESHTASTIC_PRIMARY_CHANNEL_SLOT ) {
                        strlcpy( config_entry.name, MESHTASTIC_DEFAULT_PRIMARY_CHANNEL, sizeof( config_entry.name ) );
                    }
                    else {
                        config_entry.enabled = false;
                    }
                    dirty = true;
                }

                if ( !config_entry.enabled ) {
                    continue;
                }

                if ( !meshtastic_decode_psk( config_entry.psk, runtime_entry.psk, psk_len ) ) {
                    if ( slot == MESHTASTIC_PRIMARY_CHANNEL_SLOT ) {
                        strlcpy( config_entry.psk, MESHTASTIC_DEFAULT_PRIMARY_PSK, sizeof( config_entry.psk ) );
                        dirty = true;
                        if ( !meshtastic_decode_psk( config_entry.psk, runtime_entry.psk, psk_len ) ) {
                            psk_len = 0;
                        }
                    }
                    else {
                        config_entry.enabled = false;
                        dirty = true;
                        continue;
                    }
                }

                runtime_entry.enabled = true;
                runtime_entry.psk_len = psk_len;
                strlcpy( runtime_entry.name, config_entry.name, sizeof( runtime_entry.name ) );
                runtime_entry.hash = meshtastic_channel_hash( runtime_entry.name, runtime_entry.psk, runtime_entry.psk_len );
                meshtastic_enabled_channel_slots[ meshtastic_enabled_channel_count++ ] = slot;
            }

            if ( meshtastic_enabled_channel_count == 0 ) {
                meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].enabled = true;
                strlcpy(
                    meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].name,
                    MESHTASTIC_DEFAULT_PRIMARY_CHANNEL,
                    sizeof( meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].name )
                );
                strlcpy(
                    meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].psk,
                    MESHTASTIC_DEFAULT_PRIMARY_PSK,
                    sizeof( meshtastic_channels_config.channels[ MESHTASTIC_PRIMARY_CHANNEL_SLOT ].psk )
                );
                meshtastic_channels_config.active_channel = MESHTASTIC_PRIMARY_CHANNEL_SLOT;
                dirty = true;
                meshtastic_load_channels();
                return;
            }

            if ( meshtastic_channels_config.active_channel >= MESHTASTIC_CHANNEL_COUNT ||
                 !meshtastic_channels[ meshtastic_channels_config.active_channel ].enabled ) {
                meshtastic_channels_config.active_channel = MESHTASTIC_PRIMARY_CHANNEL_SLOT;
                dirty = true;
            }

            meshtastic_active_channel_slot = meshtastic_channels_config.active_channel;

            if ( dirty ) {
                meshtastic_channels_config.save();
            }
        }

        float meshtastic_frequency( void );
        bool meshtastic_start_receive( void );
        void meshtastic_update_status( const char *fmt, ... );

        bool meshtastic_apply_primary_frequency( void ) {
            if ( !meshtastic_radio_ready ) {
                return( false );
            }

            const int state = meshtastic_radio.setFrequency( meshtastic_frequency() );
            if ( state != RADIOLIB_ERR_NONE ) {
                meshtastic_update_status( "Freq update failed %d", state );
                return( false );
            }

            if ( !meshtastic_tx_active ) {
                meshtastic_start_receive();
            }
            return( true );
        }

        float meshtastic_frequency( void ) {
            const float freq_start = 902.0f;
            const float freq_end = 928.0f;
            const float spacing = MESHTASTIC_BW_KHZ / 1000.0f;
            const uint32_t num_channels = (uint32_t)floor( ( freq_end - freq_start ) / spacing );
            const uint32_t channel_num = meshtastic_djb2_hash( meshtastic_primary_channel_name() ) % num_channels;

            return( freq_start + ( MESHTASTIC_BW_KHZ / 2000.0f ) + ( channel_num * spacing ) );
        }

        void meshtastic_update_status( const char *fmt, ... ) {
            va_list ap;

            va_start( ap, fmt );
            vsnprintf( meshtastic_status, sizeof( meshtastic_status ), fmt, ap );
            va_end( ap );
        }

        void meshtastic_store_last_message( const char *sender, const char *text ) {
            strncpy( meshtastic_last_message_sender, sender ? sender : "", sizeof( meshtastic_last_message_sender ) - 1 );
            meshtastic_last_message_sender[ sizeof( meshtastic_last_message_sender ) - 1 ] = '\0';
            strncpy( meshtastic_last_message_text, text ? text : "", sizeof( meshtastic_last_message_text ) - 1 );
            meshtastic_last_message_text[ sizeof( meshtastic_last_message_text ) - 1 ] = '\0';
        }

        size_t meshtastic_write_varint( uint32_t value, uint8_t *buffer, size_t max_len ) {
            size_t written = 0;

            do {
                if ( written >= max_len ) {
                    return( 0 );
                }
                uint8_t byte = value & 0x7F;
                value >>= 7;
                if ( value ) {
                    byte |= 0x80;
                }
                buffer[ written++ ] = byte;
            } while( value );

            return( written );
        }

        bool meshtastic_read_varint( const uint8_t *buffer, size_t len, size_t &offset, uint32_t &value ) {
            uint32_t result = 0;
            uint8_t shift = 0;

            while ( offset < len && shift < 35 ) {
                const uint8_t byte = buffer[ offset++ ];
                result |= (uint32_t)( byte & 0x7F ) << shift;
                if ( ( byte & 0x80 ) == 0 ) {
                    value = result;
                    return( true );
                }
                shift += 7;
            }
            return( false );
        }

        bool meshtastic_skip_field( const uint8_t *buffer, size_t len, size_t &offset, uint8_t wire_type ) {
            uint32_t field_len = 0;

            switch( wire_type ) {
                case 0:
                    return( meshtastic_read_varint( buffer, len, offset, field_len ) );
                case 1:
                    if ( offset + 8 > len ) {
                        return( false );
                    }
                    offset += 8;
                    return( true );
                case 2:
                    if ( !meshtastic_read_varint( buffer, len, offset, field_len ) ) {
                        return( false );
                    }
                    if ( offset + field_len > len ) {
                        return( false );
                    }
                    offset += field_len;
                    return( true );
                case 5:
                    if ( offset + 4 > len ) {
                        return( false );
                    }
                    offset += 4;
                    return( true );
            }
            return( false );
        }

        size_t meshtastic_encode_text_message( uint8_t *buffer, size_t max_len, const char *text, uint32_t dest, uint32_t source ) {
            const size_t text_len = strlen( text );
            size_t offset = 0;
            size_t written = 0;

            if ( text_len == 0 || text_len > MESHTASTIC_MAX_TEXT_LEN ) {
                return( 0 );
            }

            if ( offset >= max_len ) {
                return( 0 );
            }
            buffer[ offset++ ] = 0x08;
            written = meshtastic_write_varint( MESHTASTIC_TEXT_MESSAGE_APP, &buffer[ offset ], max_len - offset );
            if ( written == 0 ) {
                return( 0 );
            }
            offset += written;

            if ( offset >= max_len ) {
                return( 0 );
            }
            buffer[ offset++ ] = 0x12;
            written = meshtastic_write_varint( (uint32_t)text_len, &buffer[ offset ], max_len - offset );
            if ( written == 0 || offset + written + text_len > max_len ) {
                return( 0 );
            }
            offset += written;
            memcpy( &buffer[ offset ], text, text_len );
            offset += text_len;

            if ( offset + 5 > max_len ) {
                return( 0 );
            }
            buffer[ offset++ ] = 0x25;
            memcpy( &buffer[ offset ], &dest, sizeof( dest ) );
            offset += sizeof( dest );

            if ( offset + 5 > max_len ) {
                return( 0 );
            }
            buffer[ offset++ ] = 0x2D;
            memcpy( &buffer[ offset ], &source, sizeof( source ) );
            offset += sizeof( source );

            return( offset );
        }

        bool meshtastic_decode_data_message( const uint8_t *buffer, size_t len, meshtastic_decoded_data_t &decoded ) {
            size_t offset = 0;

            while ( offset < len ) {
                uint32_t key = 0;

                if ( !meshtastic_read_varint( buffer, len, offset, key ) ) {
                    return( false );
                }

                const uint32_t field_num = key >> 3;
                const uint8_t wire_type = key & 0x07;

                switch( field_num ) {
                    case 1:
                        if ( wire_type != 0 || !meshtastic_read_varint( buffer, len, offset, decoded.portnum ) ) {
                            return( false );
                        }
                        break;
                    case 2: {
                        uint32_t payload_len = 0;

                        if ( wire_type != 2 || !meshtastic_read_varint( buffer, len, offset, payload_len ) ) {
                            return( false );
                        }
                        if ( offset + payload_len > len ) {
                            return( false );
                        }
                        decoded.payload_len = payload_len < sizeof( decoded.payload ) ? payload_len : sizeof( decoded.payload );
                        memcpy( decoded.payload, &buffer[ offset ], decoded.payload_len );
                        offset += payload_len;
                        break;
                    }
                    case 4:
                        if ( wire_type != 5 || offset + sizeof( decoded.dest ) > len ) {
                            return( false );
                        }
                        memcpy( &decoded.dest, &buffer[ offset ], sizeof( decoded.dest ) );
                        offset += sizeof( decoded.dest );
                        break;
                    case 5:
                        if ( wire_type != 5 || offset + sizeof( decoded.source ) > len ) {
                            return( false );
                        }
                        memcpy( &decoded.source, &buffer[ offset ], sizeof( decoded.source ) );
                        offset += sizeof( decoded.source );
                        break;
                    default:
                        if ( !meshtastic_skip_field( buffer, len, offset, wire_type ) ) {
                            return( false );
                        }
                        break;
                }
            }

            decoded.valid = decoded.portnum != 0 && decoded.payload_len > 0;
            return( decoded.valid );
        }

        bool meshtastic_decode_text_message( const meshtastic_decoded_data_t &data, meshtastic_decoded_text_t &decoded ) {
            if ( !data.valid || data.portnum != MESHTASTIC_TEXT_MESSAGE_APP || data.payload_len == 0 ) {
                return( false );
            }

            decoded.valid = true;
            decoded.portnum = data.portnum;
            decoded.dest = data.dest;
            decoded.source = data.source;
            decoded.text_len = data.payload_len < ( sizeof( decoded.text ) - 1 ) ? data.payload_len : ( sizeof( decoded.text ) - 1 );
            memcpy( decoded.text, data.payload, decoded.text_len );
            decoded.text[ decoded.text_len ] = '\0';
            return( true );
        }

        bool meshtastic_decode_position_message( const uint8_t *buffer, size_t len, meshtastic_decoded_position_t &decoded ) {
            size_t offset = 0;

            while ( offset < len ) {
                uint32_t key = 0;

                if ( !meshtastic_read_varint( buffer, len, offset, key ) ) {
                    return( false );
                }

                const uint32_t field_num = key >> 3;
                const uint8_t wire_type = key & 0x07;

                switch( field_num ) {
                    case 1:
                        if ( wire_type != 5 || offset + sizeof( decoded.latitude_i ) > len ) {
                            return( false );
                        }
                        memcpy( &decoded.latitude_i, &buffer[ offset ], sizeof( decoded.latitude_i ) );
                        offset += sizeof( decoded.latitude_i );
                        break;
                    case 2:
                        if ( wire_type != 5 || offset + sizeof( decoded.longitude_i ) > len ) {
                            return( false );
                        }
                        memcpy( &decoded.longitude_i, &buffer[ offset ], sizeof( decoded.longitude_i ) );
                        offset += sizeof( decoded.longitude_i );
                        break;
                    case 3: {
                        uint32_t altitude = 0;

                        if ( wire_type != 0 || !meshtastic_read_varint( buffer, len, offset, altitude ) ) {
                            return( false );
                        }
                        decoded.altitude = (int32_t)altitude;
                        decoded.has_altitude = true;
                        break;
                    }
                    default:
                        if ( !meshtastic_skip_field( buffer, len, offset, wire_type ) ) {
                            return( false );
                        }
                        break;
                }
            }

            decoded.valid = decoded.latitude_i != 0 || decoded.longitude_i != 0;
            return( decoded.valid );
        }

        void meshtastic_crypt_payload( uint32_t from_node, uint32_t packet_id, uint8_t *data, size_t len, const uint8_t *psk, size_t psk_len ) {
            mbedtls_aes_context aes;
            uint8_t nonce[ 16 ] = { 0 };
            uint8_t stream_block[ 16 ] = { 0 };
            size_t nc_off = 0;
            const uint64_t packet_id_64 = packet_id;

            if ( !data || len == 0 || psk_len == 0 ) {
                return;
            }

            memcpy( nonce, &packet_id_64, sizeof( packet_id_64 ) );
            memcpy( nonce + sizeof( packet_id_64 ), &from_node, sizeof( from_node ) );

            mbedtls_aes_init( &aes );
            mbedtls_aes_setkey_enc( &aes, psk, psk_len * 8 );
            mbedtls_aes_crypt_ctr( &aes, len, &nc_off, nonce, stream_block, data, data );
            mbedtls_aes_free( &aes );
        }

        bool meshtastic_queue_notification( const char *title, const char *body ) {
            StaticJsonDocument< 512 > doc;
            char json[ 512 ];

            doc["t"] = "notify";
            doc["src"] = "Meshtastic";
            doc["title"] = title;
            doc["body"] = body;

            const size_t json_len = serializeJson( doc, json, sizeof( json ) );
            if ( json_len == 0 || json_len >= sizeof( json ) ) {
                return( false );
            }

            return( bluetooth_message_queue_msg( json ) );
        }

        uint32_t meshtastic_generate_packet_id( void ) {
            meshtastic_packet_counter++;
            meshtastic_packet_counter &= 0x3FF;

            return( meshtastic_packet_counter | ( (uint32_t)esp_random() << 10 ) );
        }

        bool meshtastic_start_receive( void ) {
            const int state = meshtastic_radio.startReceive();

            meshtastic_tx_active = false;
            meshtastic_radio_receiving = state == RADIOLIB_ERR_NONE;
            if ( !meshtastic_radio_receiving ) {
                meshtastic_update_status( "RX error %d", state );
            }
            return( meshtastic_radio_receiving );
        }

        void IRAM_ATTR meshtastic_radio_isr( void ) {
            meshtastic_radio_irq = true;
        }

        bool meshtastic_handle_rx( void ) {
            uint8_t packet[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };
            meshtastic_decoded_data_t data;
            meshtastic_decoded_text_t decoded;
            meshtastic_decoded_position_t position;
            char sender[ 24 ];
            const meshtastic_runtime_channel_t *rx_channel = NULL;

            const size_t packet_len = meshtastic_radio.getPacketLength();
            if ( packet_len < sizeof( meshtastic_packet_header_t ) || packet_len > sizeof( packet ) ) {
                meshtastic_update_status( "Bad packet length %u", (unsigned)packet_len );
                return( false );
            }

            if ( meshtastic_radio.readData( packet, packet_len ) != RADIOLIB_ERR_NONE ) {
                meshtastic_update_status( "RX read failed" );
                return( false );
            }

            const meshtastic_packet_header_t *header = (const meshtastic_packet_header_t *)packet;
            if ( header->from == 0 || header->from == meshtastic_node_id ) {
                return( false );
            }

            const int8_t channel_slot = meshtastic_find_channel_slot_for_hash( header->channel );
            if ( channel_slot < 0 ) {
                return( false );
            }
            rx_channel = &meshtastic_channels[ channel_slot ];

            const size_t payload_len = packet_len - sizeof( meshtastic_packet_header_t );
            uint8_t payload[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };

            memcpy( payload, packet + sizeof( meshtastic_packet_header_t ), payload_len );
            meshtastic_crypt_payload( header->from, header->id, payload, payload_len, rx_channel->psk, rx_channel->psk_len );

            if ( !meshtastic_decode_data_message( payload, payload_len, data ) ) {
                return( false );
            }

            if ( data.dest != 0 && data.dest != MESHTASTIC_BROADCAST && data.dest != meshtastic_node_id ) {
                return( false );
            }

            meshtastic_last_peer = header->from;
            meshtastic_last_rssi = (int32_t)lround( meshtastic_radio.getRSSI() );
            meshtastic_last_snr = meshtastic_radio.getSNR();

            snprintf( sender, sizeof( sender ), "!%08" PRIX32, header->from );

            if ( meshtastic_decode_text_message( data, decoded ) ) {
                meshtastic_store_last_message( sender, decoded.text );
                meshtastic_queue_notification( sender, decoded.text );
                xnode_send_meshtastic_rx( sender, decoded.text );
                if ( meshtastic_text_rx_callback ) {
                    meshtastic_text_rx_callback(
                        header->from,
                        data.dest ? data.dest : header->to,
                        (uint8_t)channel_slot,
                        header->id,
                        meshtastic_last_rssi,
                        meshtastic_last_snr,
                        decoded.text
                    );
                }
                meshtastic_update_status( "RX %s %s", rx_channel->name, sender );
                return( true );
            }

            if ( data.portnum == MESHTASTIC_POSITION_APP &&
                 meshtastic_decode_position_message( data.payload, data.payload_len, position ) ) {
                char body[ 128 ];
                const double lat = position.latitude_i * 1e-7;
                const double lon = position.longitude_i * 1e-7;

                osmmap_set_external_marker( lon, lat, sender );
                xnode_send_location_update( lat, lon, sender );
                if ( position.has_altitude ) {
                    snprintf( body, sizeof( body ), "Pos %.5f %.5f alt %dm", lat, lon, (int)position.altitude );
                }
                else {
                    snprintf( body, sizeof( body ), "Pos %.5f %.5f", lat, lon );
                }
                meshtastic_store_last_message( sender, body );
                meshtastic_queue_notification( sender, body );
                meshtastic_update_status( "RX %s %s", rx_channel->name, sender );
                return( true );
            }

            meshtastic_update_status( "RX %s port %lu", rx_channel->name, (unsigned long)data.portnum );
            return( false );
        }

        bool meshtastic_powermgm_event_cb( EventBits_t event, void *arg ) {
            switch( event ) {
                case POWERMGM_STANDBY:
                    if ( meshtastic_radio_ready ) {
                        meshtastic_radio.sleep();
                        meshtastic_radio_receiving = false;
                        meshtastic_update_status( "Meshtastic sleeping" );
                    }
                    break;
                case POWERMGM_WAKEUP:
                case POWERMGM_SILENCE_WAKEUP:
                    if ( meshtastic_radio_ready && !meshtastic_tx_active ) {
                        meshtastic_radio.standby();
                        meshtastic_start_receive();
                        meshtastic_update_status( "Mesh ready" );
                    }
                    break;
            }
            return( true );
        }

        bool meshtastic_powermgm_loop_cb( EventBits_t event, void *arg ) {
            if ( !meshtastic_radio_ready || !meshtastic_radio_irq ) {
                return( true );
            }

            meshtastic_radio_irq = false;

            if ( meshtastic_tx_active ) {
                meshtastic_radio.finishTransmit();
                meshtastic_tx_active = false;
                meshtastic_update_status( "TX sent %s", meshtastic_pending_channel_name[ 0 ] ? meshtastic_pending_channel_name : "mesh" );

                if ( meshtastic_pending_text[ 0 ] ) {
                    meshtastic_store_last_message( "Me", meshtastic_pending_text );
                    meshtastic_queue_notification( "Me", meshtastic_pending_text );
                    meshtastic_pending_text[ 0 ] = '\0';
                }
                meshtastic_pending_channel_name[ 0 ] = '\0';
                meshtastic_start_receive();
            }
            else {
                meshtastic_handle_rx();
                meshtastic_start_receive();
            }

            return( true );
        }
    }

    void meshtastic_service_setup( void ) {
        if ( meshtastic_service_started ) {
            return;
        }

        meshtastic_service_started = true;
        meshtastic_load_channels();
        meshtastic_node_id = (uint32_t)( ESP.getEfuseMac() & 0xFFFFFFFFULL );
        if ( meshtastic_node_id == 0 || meshtastic_node_id == MESHTASTIC_BROADCAST ) {
            meshtastic_node_id ^= 0x5A5A1234UL;
        }
        meshtastic_packet_counter = esp_random() & 0x3FF;

        meshtastic_update_status( "Initializing radio" );

        const int state = meshtastic_radio.begin(
            meshtastic_frequency(),
            MESHTASTIC_BW_KHZ,
            MESHTASTIC_SF,
            MESHTASTIC_CR,
            MESHTASTIC_SYNC_WORD,
            MESHTASTIC_TX_POWER,
            MESHTASTIC_PREAMBLE,
            MESHTASTIC_TCXO_VOLTAGE,
            false
        );

        if ( state != RADIOLIB_ERR_NONE ) {
            meshtastic_update_status( "Radio init failed %d", state );
            meshtastic_radio_ready = false;
        }
        else {
            meshtastic_radio_ready = true;
            meshtastic_radio.setCurrentLimit( 140.0f );
            meshtastic_radio.setCRC( false );
            meshtastic_radio.setDio2AsRfSwitch( true );
            meshtastic_radio.setDio1Action( meshtastic_radio_isr );
            meshtastic_start_receive();
            meshtastic_update_status( "Mesh ready" );
        }

        powermgm_register_cb(
            POWERMGM_STANDBY | POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP,
            meshtastic_powermgm_event_cb,
            "meshtastic event"
        );
        powermgm_register_loop_cb(
            POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP,
            meshtastic_powermgm_loop_cb,
            "meshtastic loop"
        );
    }

    static bool meshtastic_service_send_text_internal( const char *text, uint32_t dest, uint8_t channel_slot ) {
        uint8_t payload[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };
        uint8_t packet[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };
        meshtastic_packet_header_t *header = (meshtastic_packet_header_t *)packet;
        const meshtastic_runtime_channel_t *tx_channel = NULL;

        if ( !meshtastic_radio_ready ) {
            meshtastic_update_status( "Radio unavailable" );
            return( false );
        }

        if ( meshtastic_tx_active ) {
            meshtastic_update_status( "Radio busy" );
            return( false );
        }

        if ( channel_slot >= MESHTASTIC_CHANNEL_COUNT ||
             !meshtastic_channels[ channel_slot ].enabled ) {
            meshtastic_update_status( "Channel unavailable" );
            return( false );
        }

        tx_channel = &meshtastic_channels[ channel_slot ];

        const size_t payload_len = meshtastic_encode_text_message(
            payload,
            sizeof( payload ),
            text,
            dest,
            meshtastic_node_id
        );

        if ( payload_len == 0 ) {
            meshtastic_update_status( "Invalid message" );
            return( false );
        }

        header->to = dest;
        header->from = meshtastic_node_id;
        header->id = meshtastic_generate_packet_id();
        header->flags = MESHTASTIC_HOP_RELIABLE |
                        ( ( MESHTASTIC_HOP_RELIABLE << MESHTASTIC_FLAG_HOP_START_SHIFT ) & MESHTASTIC_FLAG_HOP_START_MASK );
        header->channel = tx_channel->hash;
        header->next_hop = 0;
        header->relay_node = 0;

        meshtastic_crypt_payload( header->from, header->id, payload, payload_len, tx_channel->psk, tx_channel->psk_len );
        memcpy( packet + sizeof( meshtastic_packet_header_t ), payload, payload_len );

        strncpy( meshtastic_pending_text, text, sizeof( meshtastic_pending_text ) - 1 );
        meshtastic_pending_text[ sizeof( meshtastic_pending_text ) - 1 ] = '\0';
        strlcpy( meshtastic_pending_channel_name, tx_channel->name, sizeof( meshtastic_pending_channel_name ) );

        const int state = meshtastic_radio.startTransmit( packet, sizeof( meshtastic_packet_header_t ) + payload_len );
        if ( state != RADIOLIB_ERR_NONE ) {
            meshtastic_pending_text[ 0 ] = '\0';
            meshtastic_pending_channel_name[ 0 ] = '\0';
            meshtastic_update_status( "TX start failed %d", state );
            return( false );
        }

        meshtastic_tx_active = true;
        meshtastic_radio_receiving = false;
        meshtastic_update_status( "Sending %s...", tx_channel->name );
        return( true );
    }

    bool meshtastic_service_send_text( const char *text ) {
        return( meshtastic_service_send_text_internal( text, MESHTASTIC_BROADCAST, meshtastic_active_channel_slot ) );
    }

    bool meshtastic_service_send_text_to( const char *text, uint32_t dest, uint8_t channel_slot ) {
        return( meshtastic_service_send_text_internal( text, dest, channel_slot ) );
    }

    bool meshtastic_service_is_ready( void ) {
        return( meshtastic_radio_ready );
    }

    bool meshtastic_service_is_receiving( void ) {
        return( meshtastic_radio_receiving );
    }

    const char *meshtastic_service_get_status( void ) {
        return( meshtastic_status );
    }

    uint8_t meshtastic_service_get_channel_count( void ) {
        return( meshtastic_enabled_channel_count );
    }

    const char *meshtastic_service_get_channel_name( uint8_t channel_index ) {
        const int8_t slot = meshtastic_slot_for_list_index( channel_index );

        if ( slot < 0 ) {
            return( "" );
        }
        return( meshtastic_channels[ slot ].name );
    }

    uint8_t meshtastic_service_get_active_channel( void ) {
        const int8_t channel_index = meshtastic_list_index_for_slot( meshtastic_active_channel_slot );

        return( channel_index >= 0 ? (uint8_t)channel_index : 0 );
    }

    bool meshtastic_service_set_active_channel( uint8_t channel_index ) {
        const int8_t slot = meshtastic_slot_for_list_index( channel_index );

        if ( slot < 0 ) {
            return( false );
        }

        meshtastic_active_channel_slot = slot;
        meshtastic_channels_config.active_channel = (uint8_t)slot;
        meshtastic_channels_config.save();
        return( true );
    }

    const char *meshtastic_service_get_active_channel_name( void ) {
        return( meshtastic_active_channel()->name );
    }

    const char *meshtastic_service_get_primary_channel_name( void ) {
        return( meshtastic_primary_channel_name() );
    }

    float meshtastic_service_get_frequency_mhz( void ) {
        return( meshtastic_frequency() );
    }

    uint32_t meshtastic_service_get_node_id( void ) {
        return( meshtastic_node_id );
    }

    uint32_t meshtastic_service_get_last_peer( void ) {
        return( meshtastic_last_peer );
    }

    int32_t meshtastic_service_get_last_rssi( void ) {
        return( meshtastic_last_rssi );
    }

    float meshtastic_service_get_last_snr( void ) {
        return( meshtastic_last_snr );
    }

    const char *meshtastic_service_get_last_message_sender( void ) {
        return( meshtastic_last_message_sender );
    }

    const char *meshtastic_service_get_last_message_text( void ) {
        return( meshtastic_last_message_text );
    }

    bool meshtastic_service_get_channel_info( uint8_t channel_slot, meshtastic_service_channel_info_t *info ) {
        uint8_t raw_psk[ MESHTASTIC_CHANNEL_PSK_B64_LEN ] = { 0 };
        size_t raw_psk_len = 0;

        if ( !info || channel_slot >= MESHTASTIC_CHANNEL_COUNT ) {
            return( false );
        }

        memset( info, 0, sizeof( *info ) );

        if ( !meshtastic_decode_base64_raw(
                 meshtastic_channels_config.channels[ channel_slot ].psk,
                 raw_psk,
                 raw_psk_len
             ) ) {
            return( false );
        }

        info->enabled = meshtastic_channels_config.channels[ channel_slot ].enabled;
        info->role = info->enabled
                         ? ( channel_slot == MESHTASTIC_PRIMARY_CHANNEL_SLOT
                                 ? MESHTASTIC_SERVICE_CHANNEL_ROLE_PRIMARY
                                 : MESHTASTIC_SERVICE_CHANNEL_ROLE_SECONDARY )
                         : MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED;
        strlcpy( info->name, meshtastic_channels_config.channels[ channel_slot ].name, sizeof( info->name ) );
        info->psk_len = raw_psk_len <= sizeof( info->psk ) ? (uint8_t)raw_psk_len : sizeof( info->psk );
        memcpy( info->psk, raw_psk, info->psk_len );
        return( true );
    }

    bool meshtastic_service_set_channel_info( uint8_t channel_slot, const meshtastic_service_channel_info_t *info ) {
        const float old_frequency = meshtastic_frequency();
        bool changed = false;

        if ( !info || channel_slot >= MESHTASTIC_CHANNEL_COUNT ) {
            return( false );
        }

        if ( channel_slot == MESHTASTIC_PRIMARY_CHANNEL_SLOT &&
             ( !info->enabled || info->role == MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED ) ) {
            return( false );
        }

        if ( info->psk_len > MESHTASTIC_SERVICE_MAX_PSK_LEN ) {
            return( false );
        }

        meshtastic_channels_config.channels[ channel_slot ].enabled =
            info->enabled && info->role != MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED;
        strlcpy(
            meshtastic_channels_config.channels[ channel_slot ].name,
            info->name,
            sizeof( meshtastic_channels_config.channels[ channel_slot ].name )
        );

        if ( !meshtastic_encode_base64_raw(
                 info->psk,
                 info->psk_len,
                 meshtastic_channels_config.channels[ channel_slot ].psk,
                 sizeof( meshtastic_channels_config.channels[ channel_slot ].psk )
             ) ) {
            return( false );
        }

        if ( !meshtastic_channels_config.channels[ channel_slot ].enabled ) {
            meshtastic_channels_config.channels[ channel_slot ].name[ 0 ] = '\0';
            meshtastic_channels_config.channels[ channel_slot ].psk[ 0 ] = '\0';
        }

        changed = true;

        if ( changed ) {
            meshtastic_channels_config.save();
            meshtastic_load_channels();

            if ( meshtastic_active_channel_slot >= MESHTASTIC_CHANNEL_COUNT ||
                 !meshtastic_channels[ meshtastic_active_channel_slot ].enabled ) {
                meshtastic_active_channel_slot = MESHTASTIC_PRIMARY_CHANNEL_SLOT;
                meshtastic_channels_config.active_channel = MESHTASTIC_PRIMARY_CHANNEL_SLOT;
                meshtastic_channels_config.save();
            }

            if ( meshtastic_radio_ready && fabsf( old_frequency - meshtastic_frequency() ) > 0.0001f ) {
                meshtastic_apply_primary_frequency();
            }
        }

        return( true );
    }

    void meshtastic_service_set_text_rx_callback( meshtastic_service_text_rx_cb_t callback ) {
        meshtastic_text_rx_callback = callback;
    }

#else

    void meshtastic_service_setup( void ) {
    }

    bool meshtastic_service_send_text( const char *text ) {
        return( false );
    }

    bool meshtastic_service_send_text_to( const char *text, uint32_t dest, uint8_t channel_slot ) {
        (void)text;
        (void)dest;
        (void)channel_slot;
        return( false );
    }

    bool meshtastic_service_is_ready( void ) {
        return( false );
    }

    bool meshtastic_service_is_receiving( void ) {
        return( false );
    }

    const char *meshtastic_service_get_status( void ) {
        return( "Meshtastic requires T-Watch S3 hardware" );
    }

    uint8_t meshtastic_service_get_channel_count( void ) {
        return( 0 );
    }

    const char *meshtastic_service_get_channel_name( uint8_t channel_index ) {
        (void)channel_index;
        return( "" );
    }

    uint8_t meshtastic_service_get_active_channel( void ) {
        return( 0 );
    }

    bool meshtastic_service_set_active_channel( uint8_t channel_index ) {
        (void)channel_index;
        return( false );
    }

    const char *meshtastic_service_get_active_channel_name( void ) {
        return( "" );
    }

    const char *meshtastic_service_get_primary_channel_name( void ) {
        return( "" );
    }

    float meshtastic_service_get_frequency_mhz( void ) {
        return( 0.0f );
    }

    uint32_t meshtastic_service_get_node_id( void ) {
        return( 0 );
    }

    uint32_t meshtastic_service_get_last_peer( void ) {
        return( 0 );
    }

    int32_t meshtastic_service_get_last_rssi( void ) {
        return( 0 );
    }

    float meshtastic_service_get_last_snr( void ) {
        return( 0.0f );
    }

    const char *meshtastic_service_get_last_message_sender( void ) {
        return( "" );
    }

    const char *meshtastic_service_get_last_message_text( void ) {
        return( "" );
    }

    bool meshtastic_service_get_channel_info( uint8_t channel_slot, meshtastic_service_channel_info_t *info ) {
        (void)channel_slot;
        (void)info;
        return( false );
    }

    bool meshtastic_service_set_channel_info( uint8_t channel_slot, const meshtastic_service_channel_info_t *info ) {
        (void)channel_slot;
        (void)info;
        return( false );
    }

    void meshtastic_service_set_text_rx_callback( meshtastic_service_text_rx_cb_t callback ) {
        (void)callback;
    }

#endif
