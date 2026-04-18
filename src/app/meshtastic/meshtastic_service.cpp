#include "config.h"
#include "meshtastic_service.h"

#if defined( USING_TWATCH_S3 )

    #include <Arduino.h>
    #include <ArduinoJson.h>
    #include <ESP.h>
    #include <LilyGoLib.h>
    #include <RadioLib.h>
    #include <inttypes.h>
    #include <mbedtls/aes.h>
    #include <stdarg.h>

    #include "app/osmmap/osmmap_app_main.h"
    #include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
    #include "hardware/ble/xnode.h"
    #include "hardware/powermgm.h"

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
        constexpr const char *MESHTASTIC_CHANNEL_NAME = "LongFast";
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
        char meshtastic_last_message_sender[ 24 ] = "";
        char meshtastic_last_message_text[ MESHTASTIC_MAX_TEXT_LEN + 1 ] = "";

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

        uint8_t meshtastic_channel_hash( void ) {
            return( meshtastic_xor_hash( (const uint8_t *)MESHTASTIC_CHANNEL_NAME, strlen( MESHTASTIC_CHANNEL_NAME ) ) ^
                    meshtastic_xor_hash( meshtastic_default_psk, sizeof( meshtastic_default_psk ) ) );
        }

        float meshtastic_frequency( void ) {
            const float freq_start = 902.0f;
            const float freq_end = 928.0f;
            const float spacing = MESHTASTIC_BW_KHZ / 1000.0f;
            const uint32_t num_channels = (uint32_t)floor( ( freq_end - freq_start ) / spacing );
            const uint32_t channel_num = meshtastic_djb2_hash( MESHTASTIC_CHANNEL_NAME ) % num_channels;

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

        void meshtastic_crypt_payload( uint32_t from_node, uint32_t packet_id, uint8_t *data, size_t len ) {
            mbedtls_aes_context aes;
            uint8_t nonce[ 16 ] = { 0 };
            uint8_t stream_block[ 16 ] = { 0 };
            size_t nc_off = 0;
            const uint64_t packet_id_64 = packet_id;

            memcpy( nonce, &packet_id_64, sizeof( packet_id_64 ) );
            memcpy( nonce + sizeof( packet_id_64 ), &from_node, sizeof( from_node ) );

            mbedtls_aes_init( &aes );
            mbedtls_aes_setkey_enc( &aes, meshtastic_default_psk, sizeof( meshtastic_default_psk ) * 8 );
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
            if ( header->from == 0 || header->from == meshtastic_node_id || header->channel != meshtastic_channel_hash() ) {
                return( false );
            }

            const size_t payload_len = packet_len - sizeof( meshtastic_packet_header_t );
            uint8_t payload[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };

            memcpy( payload, packet + sizeof( meshtastic_packet_header_t ), payload_len );
            meshtastic_crypt_payload( header->from, header->id, payload, payload_len );

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
                meshtastic_update_status( "RX text %s", sender );
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
                meshtastic_update_status( "RX pos %s", sender );
                return( true );
            }

            meshtastic_update_status( "RX port %lu", (unsigned long)data.portnum );
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
                        meshtastic_update_status( "Public mesh ready" );
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
                meshtastic_update_status( "TX sent" );

                if ( meshtastic_pending_text[ 0 ] ) {
                    meshtastic_store_last_message( "Me", meshtastic_pending_text );
                    meshtastic_queue_notification( "Me", meshtastic_pending_text );
                    meshtastic_pending_text[ 0 ] = '\0';
                }
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
            meshtastic_update_status( "Public mesh ready" );
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

    bool meshtastic_service_send_text( const char *text ) {
        uint8_t payload[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };
        uint8_t packet[ MESHTASTIC_MAX_PACKET_LEN ] = { 0 };
        meshtastic_packet_header_t *header = (meshtastic_packet_header_t *)packet;

        if ( !meshtastic_radio_ready ) {
            meshtastic_update_status( "Radio unavailable" );
            return( false );
        }

        if ( meshtastic_tx_active ) {
            meshtastic_update_status( "Radio busy" );
            return( false );
        }

        const size_t payload_len = meshtastic_encode_text_message(
            payload,
            sizeof( payload ),
            text,
            MESHTASTIC_BROADCAST,
            meshtastic_node_id
        );

        if ( payload_len == 0 ) {
            meshtastic_update_status( "Invalid message" );
            return( false );
        }

        header->to = MESHTASTIC_BROADCAST;
        header->from = meshtastic_node_id;
        header->id = meshtastic_generate_packet_id();
        header->flags = MESHTASTIC_HOP_RELIABLE |
                        ( ( MESHTASTIC_HOP_RELIABLE << MESHTASTIC_FLAG_HOP_START_SHIFT ) & MESHTASTIC_FLAG_HOP_START_MASK );
        header->channel = meshtastic_channel_hash();
        header->next_hop = 0;
        header->relay_node = 0;

        meshtastic_crypt_payload( header->from, header->id, payload, payload_len );
        memcpy( packet + sizeof( meshtastic_packet_header_t ), payload, payload_len );

        strncpy( meshtastic_pending_text, text, sizeof( meshtastic_pending_text ) - 1 );
        meshtastic_pending_text[ sizeof( meshtastic_pending_text ) - 1 ] = '\0';

        const int state = meshtastic_radio.startTransmit( packet, sizeof( meshtastic_packet_header_t ) + payload_len );
        if ( state != RADIOLIB_ERR_NONE ) {
            meshtastic_pending_text[ 0 ] = '\0';
            meshtastic_update_status( "TX start failed %d", state );
            return( false );
        }

        meshtastic_tx_active = true;
        meshtastic_radio_receiving = false;
        meshtastic_update_status( "Sending..." );
        return( true );
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

#else

    void meshtastic_service_setup( void ) {
    }

    bool meshtastic_service_send_text( const char *text ) {
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

#endif
