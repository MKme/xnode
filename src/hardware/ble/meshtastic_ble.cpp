#include "config.h"
#include "meshtastic_ble.h"

#if !defined( NATIVE_64BIT ) && defined( USING_TWATCH_S3 )

    #include <Arduino.h>
    #include <ESP.h>
    #include <NimBLEDevice.h>
    #include <ctype.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/queue.h>
    #include <inttypes.h>
    #include <string.h>

    #include "app/meshtastic/meshtastic_service.h"
    #include "hardware/blectl.h"
    #include "hardware/device.h"
    #include "meshtastic/admin.pb.h"
    #include "meshtastic/channel.pb.h"
    #include "meshtastic/config.pb.h"
    #include "meshtastic/device_ui.pb.h"
    #include "meshtastic/mesh.pb.h"
    #include "meshtastic/module_config.pb.h"
    #include "meshtastic/portnums.pb.h"
    #include "pb_decode.h"
    #include "pb_encode.h"

    namespace {
        constexpr char MESHTASTIC_BLE_SERVICE_UUID[] = "6ba1b218-15a8-461f-9fa8-5dcae273eafd";
        constexpr char MESHTASTIC_BLE_TORADIO_UUID[] = "f75c76d2-129e-4dad-a1dd-7866124401e7";
        constexpr char MESHTASTIC_BLE_FROMRADIO_UUID[] = "2c55e69e-4993-11ed-b878-0242ac120002";
        constexpr char MESHTASTIC_BLE_FROMNUM_UUID[] = "ed9da18c-a800-4f66-a670-aa7547e34453";
        constexpr char MESHTASTIC_BLE_LOGRADIO_UUID[] = "5a3d6e49-06e6-4423-9944-e9de8cdf9547";
        constexpr uint32_t MESHTASTIC_BROADCAST_NODE = 0xFFFFFFFFUL;
        constexpr uint8_t MESHTASTIC_CHANNEL_SLOTS = 8;
        constexpr uint8_t MESHTASTIC_CONFIG_VARIANTS = 10;
        constexpr uint8_t MESHTASTIC_MODULE_CONFIG_VARIANTS = 16;
        constexpr uint8_t MESHTASTIC_FROMRADIO_QUEUE_DEPTH = 48;
        constexpr uint8_t MESHTASTIC_PRIMARY_CHANNEL_SLOT = 0;
        constexpr int8_t MESHTASTIC_QUEUE_STATUS_OK = 0;
        constexpr int8_t MESHTASTIC_QUEUE_STATUS_ERROR = 1;
        constexpr char MESHTASTIC_FIRMWARE_VERSION[] = "2.7.17-xnode";
        constexpr uint16_t MESHTASTIC_BATTERY_SERVICE_UUID = 0x180F;

        typedef struct {
            size_t len;
            uint8_t data[ meshtastic_FromRadio_size ];
        } meshtastic_ble_frame_t;

        struct meshtastic_ble_config_store_t {
            meshtastic_Config_DeviceConfig device;
            meshtastic_Config_PositionConfig position;
            meshtastic_Config_PowerConfig power;
            meshtastic_Config_NetworkConfig network;
            meshtastic_Config_DisplayConfig display;
            meshtastic_Config_LoRaConfig lora;
            meshtastic_Config_BluetoothConfig bluetooth;
            meshtastic_Config_SecurityConfig security;
            meshtastic_Config_SessionkeyConfig sessionkey;
            meshtastic_DeviceUIConfig device_ui;
        };

        struct meshtastic_ble_module_config_store_t {
            meshtastic_ModuleConfig_MQTTConfig mqtt;
            meshtastic_ModuleConfig_SerialConfig serial;
            meshtastic_ModuleConfig_ExternalNotificationConfig external_notification;
            meshtastic_ModuleConfig_StoreForwardConfig store_forward;
            meshtastic_ModuleConfig_RangeTestConfig range_test;
            meshtastic_ModuleConfig_TelemetryConfig telemetry;
            meshtastic_ModuleConfig_CannedMessageConfig canned_message;
            meshtastic_ModuleConfig_AudioConfig audio;
            meshtastic_ModuleConfig_RemoteHardwareConfig remote_hardware;
            meshtastic_ModuleConfig_NeighborInfoConfig neighbor_info;
            meshtastic_ModuleConfig_AmbientLightingConfig ambient_lighting;
            meshtastic_ModuleConfig_DetectionSensorConfig detection_sensor;
            meshtastic_ModuleConfig_PaxcounterConfig paxcounter;
            meshtastic_ModuleConfig_StatusMessageConfig statusmessage;
            meshtastic_ModuleConfig_TrafficManagementConfig traffic_management;
            meshtastic_ModuleConfig_TAKConfig tak;
        };

        NimBLECharacteristic *meshtastic_to_radio_characteristic = NULL;
        NimBLECharacteristic *meshtastic_from_radio_characteristic = NULL;
        NimBLECharacteristic *meshtastic_from_num_characteristic = NULL;
        NimBLECharacteristic *meshtastic_log_radio_characteristic = NULL;
        QueueHandle_t meshtastic_from_radio_queue = NULL;
        meshtastic_ble_config_store_t meshtastic_ble_config_store = {};
        meshtastic_ble_module_config_store_t meshtastic_ble_module_config_store = {};
        uint32_t meshtastic_ble_from_radio_num = 0;
        char meshtastic_ble_owner_long_name[ sizeof( ( (meshtastic_User *)0 )->long_name ) ] = { 0 };
        char meshtastic_ble_owner_short_name[ sizeof( ( (meshtastic_User *)0 )->short_name ) ] = { 0 };
        const uint8_t meshtastic_ble_empty_value[ 1 ] = { 0 };

        template < typename T >
        bool meshtastic_ble_set_bytes( T &field, const uint8_t *data, size_t data_len ) {
            if ( data_len > sizeof( field.bytes ) ) {
                return( false );
            }
            field.size = data_len;
            if ( data && data_len ) {
                memcpy( field.bytes, data, data_len );
            }
            return( true );
        }

        bool meshtastic_ble_encode_message( const pb_msgdesc_t *fields, const void *src, uint8_t *dst, size_t dst_len, size_t &written ) {
            pb_ostream_t stream = pb_ostream_from_buffer( dst, dst_len );

            written = 0;
            if ( !pb_encode( &stream, fields, src ) ) {
                return( false );
            }
            written = stream.bytes_written;
            return( true );
        }

        bool meshtastic_ble_decode_message( const pb_msgdesc_t *fields, void *dst, const uint8_t *src, size_t src_len ) {
            pb_istream_t stream = pb_istream_from_buffer( src, src_len );

            return( pb_decode( &stream, fields, dst ) );
        }

        uint32_t meshtastic_ble_current_time_seconds( void ) {
            return( millis() / 1000UL );
        }

        uint32_t meshtastic_ble_current_node_id( void ) {
            uint32_t node_id = meshtastic_service_get_node_id();

            if ( node_id == 0 || node_id == MESHTASTIC_BROADCAST_NODE ) {
                node_id = (uint32_t)( ESP.getEfuseMac() & 0xFFFFFFFFULL );
            }
            if ( node_id == 0 || node_id == MESHTASTIC_BROADCAST_NODE ) {
                node_id ^= 0x5A5A1234UL;
            }
            return( node_id );
        }

        bool meshtastic_ble_is_connected( void ) {
            NimBLEServer *server = blectl_get_ble_server();

            return( server && server->getConnectedCount() > 0 );
        }

        void meshtastic_ble_build_short_name( const char *source, char *out, size_t out_len ) {
            size_t out_index = 0;

            if ( !out || out_len == 0 ) {
                return;
            }

            out[ 0 ] = '\0';
            if ( !source || source[ 0 ] == '\0' ) {
                snprintf( out, out_len, "%s", "XN" );
                return;
            }

            for ( size_t i = 0; source[ i ] != '\0' && out_index + 1 < out_len; i++ ) {
                if ( isalnum( (unsigned char)source[ i ] ) ) {
                    out[ out_index++ ] = (char)toupper( (unsigned char)source[ i ] );
                }
            }

            if ( out_index == 0 ) {
                snprintf( out, out_len, "%s", "XN" );
                return;
            }

            out[ out_index ] = '\0';
        }

        void meshtastic_ble_sync_owner_from_device( void ) {
            if ( meshtastic_ble_owner_long_name[ 0 ] == '\0' ) {
                snprintf(
                    meshtastic_ble_owner_long_name,
                    sizeof( meshtastic_ble_owner_long_name ),
                    "%s",
                    device_get_name()
                );
            }
            if ( meshtastic_ble_owner_short_name[ 0 ] == '\0' ) {
                meshtastic_ble_build_short_name(
                    meshtastic_ble_owner_long_name,
                    meshtastic_ble_owner_short_name,
                    sizeof( meshtastic_ble_owner_short_name )
                );
            }
        }

        void meshtastic_ble_reset_shadow_config( void ) {
            memset( &meshtastic_ble_config_store, 0, sizeof( meshtastic_ble_config_store ) );
            memset( &meshtastic_ble_module_config_store, 0, sizeof( meshtastic_ble_module_config_store ) );
            memset( meshtastic_ble_owner_long_name, 0, sizeof( meshtastic_ble_owner_long_name ) );
            memset( meshtastic_ble_owner_short_name, 0, sizeof( meshtastic_ble_owner_short_name ) );

            meshtastic_ble_sync_owner_from_device();

            meshtastic_ble_config_store.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
            meshtastic_ble_config_store.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
            meshtastic_ble_config_store.device.buzzer_mode = meshtastic_Config_DeviceConfig_BuzzerMode_ALL_ENABLED;

            meshtastic_ble_config_store.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;

            meshtastic_ble_config_store.lora.use_preset = true;
            meshtastic_ble_config_store.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
            meshtastic_ble_config_store.lora.bandwidth = 250;
            meshtastic_ble_config_store.lora.spread_factor = 11;
            meshtastic_ble_config_store.lora.coding_rate = 5;
            meshtastic_ble_config_store.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
            meshtastic_ble_config_store.lora.hop_limit = 3;
            meshtastic_ble_config_store.lora.tx_enabled = true;
            meshtastic_ble_config_store.lora.tx_power = 22;

            meshtastic_ble_config_store.bluetooth.enabled = true;
            meshtastic_ble_config_store.bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
            meshtastic_ble_config_store.bluetooth.fixed_pin = 0;

            meshtastic_ble_config_store.security.admin_channel_enabled = true;
            meshtastic_ble_config_store.security.serial_enabled = false;
            meshtastic_ble_config_store.security.debug_log_api_enabled = false;
        }

        void meshtastic_ble_refresh_runtime_config( void ) {
            meshtastic_ble_config_store.lora.use_preset = true;
            meshtastic_ble_config_store.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
            meshtastic_ble_config_store.lora.bandwidth = 250;
            meshtastic_ble_config_store.lora.spread_factor = 11;
            meshtastic_ble_config_store.lora.coding_rate = 5;
            meshtastic_ble_config_store.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
            meshtastic_ble_config_store.lora.hop_limit = 3;
            meshtastic_ble_config_store.lora.tx_enabled = true;
            meshtastic_ble_config_store.lora.tx_power = 22;
            meshtastic_ble_config_store.lora.override_frequency = meshtastic_service_get_frequency_mhz();

            meshtastic_ble_config_store.bluetooth.enabled = true;
            if ( meshtastic_ble_config_store.bluetooth.mode < meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN ||
                 meshtastic_ble_config_store.bluetooth.mode > meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN ) {
                meshtastic_ble_config_store.bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
            }

            meshtastic_ble_config_store.security.admin_channel_enabled = true;
            meshtastic_ble_config_store.security.serial_enabled = false;
            meshtastic_ble_config_store.security.debug_log_api_enabled = false;
        }

        void meshtastic_ble_set_from_num_value( uint32_t id ) {
            uint8_t raw[ 4 ] = {
                (uint8_t)( id & 0xFF ),
                (uint8_t)( ( id >> 8 ) & 0xFF ),
                (uint8_t)( ( id >> 16 ) & 0xFF ),
                (uint8_t)( ( id >> 24 ) & 0xFF )
            };

            if ( meshtastic_from_num_characteristic ) {
                meshtastic_from_num_characteristic->setValue( raw, sizeof( raw ) );
            }
        }

        uint32_t meshtastic_ble_hash32( const uint8_t *data, size_t len, uint32_t seed ) {
            uint32_t hash = seed;

            for ( size_t i = 0; i < len; i++ ) {
                hash = ( ( hash << 5 ) + hash ) + data[ i ];
            }
            return( hash );
        }

        uint32_t meshtastic_ble_channel_id( const char *name, const uint8_t *psk, size_t psk_len ) {
            uint32_t hash = 5381;
            const char *channel_name = name && name[ 0 ] ? name : "X";

            hash = meshtastic_ble_hash32( (const uint8_t *)channel_name, strlen( channel_name ), hash );
            return( meshtastic_ble_hash32( psk, psk_len, hash ) );
        }

        meshtastic_Channel_Role meshtastic_ble_proto_role_for_service_role( uint8_t service_role ) {
            switch( service_role ) {
                case MESHTASTIC_SERVICE_CHANNEL_ROLE_PRIMARY:
                    return( meshtastic_Channel_Role_PRIMARY );
                case MESHTASTIC_SERVICE_CHANNEL_ROLE_SECONDARY:
                    return( meshtastic_Channel_Role_SECONDARY );
                default:
                    return( meshtastic_Channel_Role_DISABLED );
            }
        }

        bool meshtastic_ble_build_user( meshtastic_User &user ) {
            const uint32_t node_id = meshtastic_ble_current_node_id();

            memset( &user, 0, sizeof( user ) );
            meshtastic_ble_sync_owner_from_device();

            snprintf( user.id, sizeof( user.id ), "!%08" PRIX32, node_id );
            snprintf( user.long_name, sizeof( user.long_name ), "%s", meshtastic_ble_owner_long_name );
            snprintf( user.short_name, sizeof( user.short_name ), "%s", meshtastic_ble_owner_short_name );
            user.hw_model = meshtastic_HardwareModel_T_WATCH_S3;
            user.role = meshtastic_ble_config_store.device.role;
            user.has_is_unmessagable = true;
            user.is_unmessagable = false;
            return( true );
        }

        bool meshtastic_ble_build_node_info( meshtastic_NodeInfo &node_info ) {
            memset( &node_info, 0, sizeof( node_info ) );

            node_info.num = meshtastic_ble_current_node_id();
            node_info.has_user = meshtastic_ble_build_user( node_info.user );
            node_info.channel = 0;
            node_info.is_favorite = true;
            node_info.last_heard = meshtastic_ble_current_time_seconds();
            return( true );
        }

        bool meshtastic_ble_build_my_info( meshtastic_MyNodeInfo &my_info ) {
            const uint64_t mac = ESP.getEfuseMac();
            uint8_t mac_bytes[ 6 ] = {
                (uint8_t)( ( mac >> 40 ) & 0xFF ),
                (uint8_t)( ( mac >> 32 ) & 0xFF ),
                (uint8_t)( ( mac >> 24 ) & 0xFF ),
                (uint8_t)( ( mac >> 16 ) & 0xFF ),
                (uint8_t)( ( mac >> 8 ) & 0xFF ),
                (uint8_t)( mac & 0xFF )
            };

            memset( &my_info, 0, sizeof( my_info ) );
            my_info.my_node_num = meshtastic_ble_current_node_id();
            my_info.reboot_count = 0;
            my_info.min_app_version = 0;
            meshtastic_ble_set_bytes( my_info.device_id, mac_bytes, sizeof( mac_bytes ) );
            snprintf( my_info.pio_env, sizeof( my_info.pio_env ), "%s", "xnode" );
            my_info.firmware_edition = meshtastic_FirmwareEdition_DIY_EDITION;
            my_info.nodedb_count = 1;
            return( true );
        }

        bool meshtastic_ble_build_metadata( meshtastic_DeviceMetadata &metadata ) {
            memset( &metadata, 0, sizeof( metadata ) );

            snprintf( metadata.firmware_version, sizeof( metadata.firmware_version ), "%s", MESHTASTIC_FIRMWARE_VERSION );
            metadata.device_state_version = 1;
            metadata.canShutdown = true;
            metadata.hasWifi = false;
            metadata.hasBluetooth = true;
            metadata.hasEthernet = false;
            metadata.role = meshtastic_ble_config_store.device.role;
            metadata.position_flags = meshtastic_ble_config_store.position.position_flags;
            metadata.hw_model = meshtastic_HardwareModel_T_WATCH_S3;
            metadata.hasRemoteHardware = false;
            metadata.hasPKC = false;
            metadata.excluded_modules = 0;
            return( true );
        }

        bool meshtastic_ble_build_channel( uint8_t slot, meshtastic_Channel &channel ) {
            meshtastic_service_channel_info_t info;

            if ( slot >= MESHTASTIC_CHANNEL_SLOTS || !meshtastic_service_get_channel_info( slot, &info ) ) {
                return( false );
            }

            memset( &channel, 0, sizeof( channel ) );
            channel.index = slot;

            if ( !info.enabled || info.role == MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED ) {
                channel.role = meshtastic_Channel_Role_DISABLED;
                channel.has_settings = false;
                return( true );
            }

            channel.role = meshtastic_ble_proto_role_for_service_role( info.role );
            channel.has_settings = true;
            snprintf( channel.settings.name, sizeof( channel.settings.name ), "%s", info.name[ 0 ] ? info.name : "X" );
            if ( !meshtastic_ble_set_bytes( channel.settings.psk, info.psk, info.psk_len ) ) {
                return( false );
            }
            channel.settings.id = meshtastic_ble_channel_id(
                channel.settings.name,
                info.psk,
                info.psk_len
            );
            return( true );
        }

        bool meshtastic_ble_build_config_by_tag( uint8_t config_tag, meshtastic_Config &config ) {
            meshtastic_ble_refresh_runtime_config();
            memset( &config, 0, sizeof( config ) );
            config.which_payload_variant = config_tag;

            switch( config_tag ) {
                case meshtastic_Config_device_tag:
                    config.payload_variant.device = meshtastic_ble_config_store.device;
                    return( true );

                case meshtastic_Config_position_tag:
                    config.payload_variant.position = meshtastic_ble_config_store.position;
                    return( true );

                case meshtastic_Config_power_tag:
                    config.payload_variant.power = meshtastic_ble_config_store.power;
                    return( true );

                case meshtastic_Config_network_tag:
                    config.payload_variant.network = meshtastic_ble_config_store.network;
                    return( true );

                case meshtastic_Config_display_tag:
                    config.payload_variant.display = meshtastic_ble_config_store.display;
                    return( true );

                case meshtastic_Config_lora_tag:
                    config.payload_variant.lora = meshtastic_ble_config_store.lora;
                    return( true );

                case meshtastic_Config_bluetooth_tag:
                    config.payload_variant.bluetooth = meshtastic_ble_config_store.bluetooth;
                    return( true );

                case meshtastic_Config_security_tag:
                    config.payload_variant.security = meshtastic_ble_config_store.security;
                    return( true );

                case meshtastic_Config_sessionkey_tag:
                    config.payload_variant.sessionkey = meshtastic_ble_config_store.sessionkey;
                    return( true );

                case meshtastic_Config_device_ui_tag:
                    config.payload_variant.device_ui = meshtastic_ble_config_store.device_ui;
                    return( true );
            }
            return( false );
        }

        bool meshtastic_ble_build_module_config_by_tag( uint8_t config_tag, meshtastic_ModuleConfig &config ) {
            memset( &config, 0, sizeof( config ) );
            config.which_payload_variant = config_tag;

            switch( config_tag ) {
                case meshtastic_ModuleConfig_mqtt_tag:
                    config.payload_variant.mqtt = meshtastic_ble_module_config_store.mqtt;
                    return( true );

                case meshtastic_ModuleConfig_serial_tag:
                    config.payload_variant.serial = meshtastic_ble_module_config_store.serial;
                    return( true );

                case meshtastic_ModuleConfig_external_notification_tag:
                    config.payload_variant.external_notification = meshtastic_ble_module_config_store.external_notification;
                    return( true );

                case meshtastic_ModuleConfig_store_forward_tag:
                    config.payload_variant.store_forward = meshtastic_ble_module_config_store.store_forward;
                    return( true );

                case meshtastic_ModuleConfig_range_test_tag:
                    config.payload_variant.range_test = meshtastic_ble_module_config_store.range_test;
                    return( true );

                case meshtastic_ModuleConfig_telemetry_tag:
                    config.payload_variant.telemetry = meshtastic_ble_module_config_store.telemetry;
                    return( true );

                case meshtastic_ModuleConfig_canned_message_tag:
                    config.payload_variant.canned_message = meshtastic_ble_module_config_store.canned_message;
                    return( true );

                case meshtastic_ModuleConfig_audio_tag:
                    config.payload_variant.audio = meshtastic_ble_module_config_store.audio;
                    return( true );

                case meshtastic_ModuleConfig_remote_hardware_tag:
                    config.payload_variant.remote_hardware = meshtastic_ble_module_config_store.remote_hardware;
                    return( true );

                case meshtastic_ModuleConfig_neighbor_info_tag:
                    config.payload_variant.neighbor_info = meshtastic_ble_module_config_store.neighbor_info;
                    return( true );

                case meshtastic_ModuleConfig_ambient_lighting_tag:
                    config.payload_variant.ambient_lighting = meshtastic_ble_module_config_store.ambient_lighting;
                    return( true );

                case meshtastic_ModuleConfig_detection_sensor_tag:
                    config.payload_variant.detection_sensor = meshtastic_ble_module_config_store.detection_sensor;
                    return( true );

                case meshtastic_ModuleConfig_paxcounter_tag:
                    config.payload_variant.paxcounter = meshtastic_ble_module_config_store.paxcounter;
                    return( true );

                case meshtastic_ModuleConfig_statusmessage_tag:
                    config.payload_variant.statusmessage = meshtastic_ble_module_config_store.statusmessage;
                    return( true );

                case meshtastic_ModuleConfig_traffic_management_tag:
                    config.payload_variant.traffic_management = meshtastic_ble_module_config_store.traffic_management;
                    return( true );

                case meshtastic_ModuleConfig_tak_tag:
                    config.payload_variant.tak = meshtastic_ble_module_config_store.tak;
                    return( true );
            }
            return( false );
        }

        uint8_t meshtastic_ble_config_tag_from_request( meshtastic_AdminMessage_ConfigType type ) {
            return( (uint8_t)type + 1 );
        }

        uint8_t meshtastic_ble_module_config_tag_from_request( meshtastic_AdminMessage_ModuleConfigType type ) {
            return( (uint8_t)type + 1 );
        }

        void meshtastic_ble_clear_from_radio_queue( void ) {
            if ( meshtastic_from_radio_queue ) {
                xQueueReset( meshtastic_from_radio_queue );
            }
        }

        void meshtastic_ble_notify_from_num( uint32_t id ) {
            if ( !meshtastic_from_num_characteristic || !meshtastic_ble_is_connected() ) {
                return;
            }

            meshtastic_ble_set_from_num_value( id );
            meshtastic_from_num_characteristic->notify();
        }

        bool meshtastic_ble_enqueue_from_radio( meshtastic_FromRadio &packet ) {
            meshtastic_ble_frame_t frame = { 0 };

            if ( !meshtastic_from_radio_queue || !meshtastic_ble_is_connected() ) {
                return( false );
            }

            packet.id = ++meshtastic_ble_from_radio_num;
            if ( !meshtastic_ble_encode_message(
                     &meshtastic_FromRadio_msg,
                     &packet,
                     frame.data,
                     sizeof( frame.data ),
                     frame.len
                 ) ) {
                log_w( "Meshtastic BLE failed to encode FromRadio packet" );
                return( false );
            }

            if ( xQueueSend( meshtastic_from_radio_queue, &frame, 0 ) != pdTRUE ) {
                meshtastic_ble_frame_t dropped = { 0 };

                if ( xQueueReceive( meshtastic_from_radio_queue, &dropped, 0 ) == pdTRUE &&
                     xQueueSend( meshtastic_from_radio_queue, &frame, 0 ) == pdTRUE ) {
                    meshtastic_ble_notify_from_num( packet.id );
                    return( true );
                }

                log_w( "Meshtastic BLE FromRadio queue full" );
                return( false );
            }

            meshtastic_ble_notify_from_num( packet.id );
            return( true );
        }

        bool meshtastic_ble_fill_data_payload( meshtastic_Data &data, meshtastic_PortNum portnum, const uint8_t *payload, size_t payload_len ) {
            memset( &data, 0, sizeof( data ) );

            if ( payload_len > sizeof( data.payload.bytes ) ) {
                return( false );
            }

            data.portnum = portnum;
            data.payload.size = payload_len;
            if ( payload && payload_len ) {
                memcpy( data.payload.bytes, payload, payload_len );
            }
            return( true );
        }

        bool meshtastic_ble_build_admin_mesh_packet( const meshtastic_AdminMessage &admin, meshtastic_MeshPacket &packet ) {
            uint8_t admin_payload[ meshtastic_AdminMessage_size ] = { 0 };
            size_t admin_payload_len = 0;

            if ( !meshtastic_ble_encode_message(
                     &meshtastic_AdminMessage_msg,
                     &admin,
                     admin_payload,
                     sizeof( admin_payload ),
                     admin_payload_len
                 ) ) {
                return( false );
            }

            memset( &packet, 0, sizeof( packet ) );
            packet.from = meshtastic_ble_current_node_id();
            packet.to = meshtastic_ble_current_node_id();
            packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            packet.rx_time = meshtastic_ble_current_time_seconds();

            if ( !meshtastic_ble_fill_data_payload(
                     packet.decoded,
                     meshtastic_PortNum_ADMIN_APP,
                     admin_payload,
                     admin_payload_len
                 ) ) {
                return( false );
            }

            packet.decoded.dest = packet.to;
            packet.decoded.source = packet.from;
            return( true );
        }

        bool meshtastic_ble_queue_admin_response( const meshtastic_AdminMessage &admin ) {
            meshtastic_MeshPacket mesh_packet;
            meshtastic_FromRadio from_radio;

            if ( !meshtastic_ble_build_admin_mesh_packet( admin, mesh_packet ) ) {
                return( false );
            }

            memset( &from_radio, 0, sizeof( from_radio ) );
            from_radio.which_payload_variant = meshtastic_FromRadio_packet_tag;
            from_radio.packet = mesh_packet;
            return( meshtastic_ble_enqueue_from_radio( from_radio ) );
        }

        void meshtastic_ble_queue_status( uint32_t mesh_packet_id, bool success ) {
            meshtastic_FromRadio from_radio;

            memset( &from_radio, 0, sizeof( from_radio ) );
            from_radio.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
            from_radio.queueStatus.res = success ? MESHTASTIC_QUEUE_STATUS_OK : MESHTASTIC_QUEUE_STATUS_ERROR;
            from_radio.queueStatus.free = meshtastic_from_radio_queue ? (uint8_t)uxQueueSpacesAvailable( meshtastic_from_radio_queue ) : 0;
            from_radio.queueStatus.maxlen = MESHTASTIC_FROMRADIO_QUEUE_DEPTH;
            from_radio.queueStatus.mesh_packet_id = mesh_packet_id;
            meshtastic_ble_enqueue_from_radio( from_radio );
        }

        void meshtastic_ble_queue_config_sequence( uint32_t nonce ) {
            meshtastic_FromRadio from_radio;
            meshtastic_MyNodeInfo my_info;
            meshtastic_NodeInfo node_info;
            meshtastic_DeviceMetadata metadata;

            meshtastic_ble_clear_from_radio_queue();

            memset( &from_radio, 0, sizeof( from_radio ) );
            if ( meshtastic_ble_build_my_info( my_info ) ) {
                from_radio.which_payload_variant = meshtastic_FromRadio_my_info_tag;
                from_radio.my_info = my_info;
                meshtastic_ble_enqueue_from_radio( from_radio );
            }

            memset( &from_radio, 0, sizeof( from_radio ) );
            from_radio.which_payload_variant = meshtastic_FromRadio_deviceuiConfig_tag;
            from_radio.deviceuiConfig = meshtastic_ble_config_store.device_ui;
            meshtastic_ble_enqueue_from_radio( from_radio );

            memset( &from_radio, 0, sizeof( from_radio ) );
            if ( meshtastic_ble_build_node_info( node_info ) ) {
                from_radio.which_payload_variant = meshtastic_FromRadio_node_info_tag;
                from_radio.node_info = node_info;
                meshtastic_ble_enqueue_from_radio( from_radio );
            }

            memset( &from_radio, 0, sizeof( from_radio ) );
            if ( meshtastic_ble_build_metadata( metadata ) ) {
                from_radio.which_payload_variant = meshtastic_FromRadio_metadata_tag;
                from_radio.metadata = metadata;
                meshtastic_ble_enqueue_from_radio( from_radio );
            }

            for ( uint8_t slot = 0; slot < MESHTASTIC_CHANNEL_SLOTS; slot++ ) {
                meshtastic_Channel channel;

                memset( &from_radio, 0, sizeof( from_radio ) );
                if ( meshtastic_ble_build_channel( slot, channel ) ) {
                    from_radio.which_payload_variant = meshtastic_FromRadio_channel_tag;
                    from_radio.channel = channel;
                    meshtastic_ble_enqueue_from_radio( from_radio );
                }
            }

            for ( uint8_t tag = 1; tag <= MESHTASTIC_CONFIG_VARIANTS; tag++ ) {
                meshtastic_Config config;

                memset( &from_radio, 0, sizeof( from_radio ) );
                if ( meshtastic_ble_build_config_by_tag( tag, config ) ) {
                    from_radio.which_payload_variant = meshtastic_FromRadio_config_tag;
                    from_radio.config = config;
                    meshtastic_ble_enqueue_from_radio( from_radio );
                }
            }

            for ( uint8_t tag = 1; tag <= MESHTASTIC_MODULE_CONFIG_VARIANTS; tag++ ) {
                meshtastic_ModuleConfig config;

                memset( &from_radio, 0, sizeof( from_radio ) );
                if ( meshtastic_ble_build_module_config_by_tag( tag, config ) ) {
                    from_radio.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
                    from_radio.moduleConfig = config;
                    meshtastic_ble_enqueue_from_radio( from_radio );
                }
            }

            memset( &from_radio, 0, sizeof( from_radio ) );
            from_radio.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
            from_radio.config_complete_id = nonce;
            meshtastic_ble_enqueue_from_radio( from_radio );
        }

        bool meshtastic_ble_handle_get_channel_request( uint32_t request_index ) {
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
            meshtastic_Channel channel;
            const uint8_t slot = request_index > 0 ? (uint8_t)( request_index - 1 ) : 0;

            if ( !meshtastic_ble_build_channel( slot, channel ) ) {
                return( false );
            }

            admin.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
            admin.get_channel_response = channel;
            return( meshtastic_ble_queue_admin_response( admin ) );
        }

        bool meshtastic_ble_handle_get_owner_request( void ) {
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;

            admin.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
            meshtastic_ble_build_user( admin.get_owner_response );
            return( meshtastic_ble_queue_admin_response( admin ) );
        }

        bool meshtastic_ble_handle_get_metadata_request( void ) {
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;

            admin.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
            meshtastic_ble_build_metadata( admin.get_device_metadata_response );
            return( meshtastic_ble_queue_admin_response( admin ) );
        }

        bool meshtastic_ble_handle_get_config_request( meshtastic_AdminMessage_ConfigType config_type ) {
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;

            admin.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
            if ( !meshtastic_ble_build_config_by_tag(
                     meshtastic_ble_config_tag_from_request( config_type ),
                     admin.get_config_response
                 ) ) {
                return( false );
            }
            return( meshtastic_ble_queue_admin_response( admin ) );
        }

        bool meshtastic_ble_handle_get_module_config_request( meshtastic_AdminMessage_ModuleConfigType config_type ) {
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;

            admin.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
            if ( !meshtastic_ble_build_module_config_by_tag(
                     meshtastic_ble_module_config_tag_from_request( config_type ),
                     admin.get_module_config_response
                 ) ) {
                return( false );
            }
            return( meshtastic_ble_queue_admin_response( admin ) );
        }

        void meshtastic_ble_apply_owner( const meshtastic_User &user ) {
            if ( user.long_name[ 0 ] ) {
                snprintf( meshtastic_ble_owner_long_name, sizeof( meshtastic_ble_owner_long_name ), "%s", user.long_name );
            }
            if ( user.short_name[ 0 ] ) {
                snprintf( meshtastic_ble_owner_short_name, sizeof( meshtastic_ble_owner_short_name ), "%s", user.short_name );
            } else {
                meshtastic_ble_build_short_name(
                    meshtastic_ble_owner_long_name,
                    meshtastic_ble_owner_short_name,
                    sizeof( meshtastic_ble_owner_short_name )
                );
            }

            if ( meshtastic_ble_owner_long_name[ 0 ] ) {
                device_set_name( meshtastic_ble_owner_long_name );
            } else if ( meshtastic_ble_owner_short_name[ 0 ] ) {
                device_set_name( meshtastic_ble_owner_short_name );
            }
        }

        bool meshtastic_ble_handle_set_owner( const meshtastic_User &user ) {
            meshtastic_ble_apply_owner( user );
            return( meshtastic_ble_handle_get_owner_request() );
        }

        void meshtastic_ble_store_config( const meshtastic_Config &config ) {
            switch( config.which_payload_variant ) {
                case meshtastic_Config_device_tag:
                    meshtastic_ble_config_store.device = config.payload_variant.device;
                    break;

                case meshtastic_Config_position_tag:
                    meshtastic_ble_config_store.position = config.payload_variant.position;
                    break;

                case meshtastic_Config_power_tag:
                    meshtastic_ble_config_store.power = config.payload_variant.power;
                    break;

                case meshtastic_Config_network_tag:
                    meshtastic_ble_config_store.network = config.payload_variant.network;
                    break;

                case meshtastic_Config_display_tag:
                    meshtastic_ble_config_store.display = config.payload_variant.display;
                    break;

                case meshtastic_Config_lora_tag:
                    meshtastic_ble_config_store.lora = config.payload_variant.lora;
                    break;

                case meshtastic_Config_bluetooth_tag:
                    meshtastic_ble_config_store.bluetooth = config.payload_variant.bluetooth;
                    break;

                case meshtastic_Config_security_tag:
                    meshtastic_ble_config_store.security = config.payload_variant.security;
                    break;

                case meshtastic_Config_sessionkey_tag:
                    meshtastic_ble_config_store.sessionkey = config.payload_variant.sessionkey;
                    break;

                case meshtastic_Config_device_ui_tag:
                    meshtastic_ble_config_store.device_ui = config.payload_variant.device_ui;
                    break;
            }
        }

        void meshtastic_ble_store_module_config( const meshtastic_ModuleConfig &config ) {
            switch( config.which_payload_variant ) {
                case meshtastic_ModuleConfig_mqtt_tag:
                    meshtastic_ble_module_config_store.mqtt = config.payload_variant.mqtt;
                    break;

                case meshtastic_ModuleConfig_serial_tag:
                    meshtastic_ble_module_config_store.serial = config.payload_variant.serial;
                    break;

                case meshtastic_ModuleConfig_external_notification_tag:
                    meshtastic_ble_module_config_store.external_notification = config.payload_variant.external_notification;
                    break;

                case meshtastic_ModuleConfig_store_forward_tag:
                    meshtastic_ble_module_config_store.store_forward = config.payload_variant.store_forward;
                    break;

                case meshtastic_ModuleConfig_range_test_tag:
                    meshtastic_ble_module_config_store.range_test = config.payload_variant.range_test;
                    break;

                case meshtastic_ModuleConfig_telemetry_tag:
                    meshtastic_ble_module_config_store.telemetry = config.payload_variant.telemetry;
                    break;

                case meshtastic_ModuleConfig_canned_message_tag:
                    meshtastic_ble_module_config_store.canned_message = config.payload_variant.canned_message;
                    break;

                case meshtastic_ModuleConfig_audio_tag:
                    meshtastic_ble_module_config_store.audio = config.payload_variant.audio;
                    break;

                case meshtastic_ModuleConfig_remote_hardware_tag:
                    meshtastic_ble_module_config_store.remote_hardware = config.payload_variant.remote_hardware;
                    break;

                case meshtastic_ModuleConfig_neighbor_info_tag:
                    meshtastic_ble_module_config_store.neighbor_info = config.payload_variant.neighbor_info;
                    break;

                case meshtastic_ModuleConfig_ambient_lighting_tag:
                    meshtastic_ble_module_config_store.ambient_lighting = config.payload_variant.ambient_lighting;
                    break;

                case meshtastic_ModuleConfig_detection_sensor_tag:
                    meshtastic_ble_module_config_store.detection_sensor = config.payload_variant.detection_sensor;
                    break;

                case meshtastic_ModuleConfig_paxcounter_tag:
                    meshtastic_ble_module_config_store.paxcounter = config.payload_variant.paxcounter;
                    break;

                case meshtastic_ModuleConfig_statusmessage_tag:
                    meshtastic_ble_module_config_store.statusmessage = config.payload_variant.statusmessage;
                    break;

                case meshtastic_ModuleConfig_traffic_management_tag:
                    meshtastic_ble_module_config_store.traffic_management = config.payload_variant.traffic_management;
                    break;

                case meshtastic_ModuleConfig_tak_tag:
                    meshtastic_ble_module_config_store.tak = config.payload_variant.tak;
                    break;
            }
        }

        bool meshtastic_ble_handle_set_config( const meshtastic_Config &config ) {
            const uint8_t config_tag = config.which_payload_variant;
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;

            meshtastic_ble_store_config( config );

            admin.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
            if ( !meshtastic_ble_build_config_by_tag( config_tag, admin.get_config_response ) ) {
                return( false );
            }
            return( meshtastic_ble_queue_admin_response( admin ) );
        }

        bool meshtastic_ble_handle_set_module_config( const meshtastic_ModuleConfig &config ) {
            const uint8_t config_tag = config.which_payload_variant;
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;

            meshtastic_ble_store_module_config( config );

            admin.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
            if ( !meshtastic_ble_build_module_config_by_tag( config_tag, admin.get_module_config_response ) ) {
                return( false );
            }
            return( meshtastic_ble_queue_admin_response( admin ) );
        }

        bool meshtastic_ble_handle_set_channel( const meshtastic_Channel &channel ) {
            meshtastic_service_channel_info_t info = { 0 };
            const uint8_t slot = channel.index < 0 ? 0 : (uint8_t)channel.index;

            if ( slot >= MESHTASTIC_CHANNEL_SLOTS ) {
                return( false );
            }

            if ( channel.role == meshtastic_Channel_Role_DISABLED || !channel.has_settings ) {
                if ( slot == MESHTASTIC_PRIMARY_CHANNEL_SLOT ) {
                    return( meshtastic_ble_handle_get_channel_request( slot + 1 ) );
                }

                info.enabled = false;
                info.role = MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED;
                if ( !meshtastic_service_set_channel_info( slot, &info ) ) {
                    return( false );
                }
                return( meshtastic_ble_handle_get_channel_request( slot + 1 ) );
            }

            info.enabled = true;
            info.role = slot == MESHTASTIC_PRIMARY_CHANNEL_SLOT
                            ? MESHTASTIC_SERVICE_CHANNEL_ROLE_PRIMARY
                            : MESHTASTIC_SERVICE_CHANNEL_ROLE_SECONDARY;
            snprintf(
                info.name,
                sizeof( info.name ),
                "%s",
                channel.settings.name[ 0 ]
                    ? channel.settings.name
                    : ( slot == MESHTASTIC_PRIMARY_CHANNEL_SLOT ? "LongFast" : "Channel" )
            );
            info.psk_len = channel.settings.psk.size <= sizeof( info.psk )
                               ? channel.settings.psk.size
                               : sizeof( info.psk );
            memcpy( info.psk, channel.settings.psk.bytes, info.psk_len );

            if ( !meshtastic_service_set_channel_info( slot, &info ) ) {
                return( false );
            }
            return( meshtastic_ble_handle_get_channel_request( slot + 1 ) );
        }

        void meshtastic_ble_handle_admin_payload( const uint8_t *payload, size_t payload_len ) {
            meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;

            if ( !payload || payload_len == 0 ) {
                return;
            }

            if ( !meshtastic_ble_decode_message( &meshtastic_AdminMessage_msg, &admin, payload, payload_len ) ) {
                log_w( "Meshtastic BLE rejected malformed AdminMessage" );
                return;
            }

            switch( admin.which_payload_variant ) {
                case meshtastic_AdminMessage_get_channel_request_tag:
                    meshtastic_ble_handle_get_channel_request( admin.get_channel_request );
                    break;

                case meshtastic_AdminMessage_get_owner_request_tag:
                    meshtastic_ble_handle_get_owner_request();
                    break;

                case meshtastic_AdminMessage_get_config_request_tag:
                    meshtastic_ble_handle_get_config_request( admin.get_config_request );
                    break;

                case meshtastic_AdminMessage_get_module_config_request_tag:
                    meshtastic_ble_handle_get_module_config_request( admin.get_module_config_request );
                    break;

                case meshtastic_AdminMessage_get_device_metadata_request_tag:
                    meshtastic_ble_handle_get_metadata_request();
                    break;

                case meshtastic_AdminMessage_set_owner_tag:
                    meshtastic_ble_handle_set_owner( admin.set_owner );
                    break;

                case meshtastic_AdminMessage_set_channel_tag:
                    meshtastic_ble_handle_set_channel( admin.set_channel );
                    break;

                case meshtastic_AdminMessage_set_config_tag:
                    meshtastic_ble_handle_set_config( admin.set_config );
                    break;

                case meshtastic_AdminMessage_set_module_config_tag:
                    meshtastic_ble_handle_set_module_config( admin.set_module_config );
                    break;

                case meshtastic_AdminMessage_begin_edit_settings_tag:
                case meshtastic_AdminMessage_commit_edit_settings_tag:
                    break;

                default:
                    log_i( "Meshtastic BLE ignored admin variant %u", (unsigned)admin.which_payload_variant );
                    break;
            }
        }

        void meshtastic_ble_handle_text_packet( const meshtastic_MeshPacket &packet ) {
            char text[ sizeof( ( (meshtastic_Data *)0 )->payload.bytes ) + 1 ] = { 0 };
            const size_t text_len = packet.decoded.payload.size <= sizeof( packet.decoded.payload.bytes )
                                        ? packet.decoded.payload.size
                                        : sizeof( packet.decoded.payload.bytes );
            const uint8_t channel_slot = packet.channel < MESHTASTIC_CHANNEL_SLOTS ? packet.channel : 0;
            const uint32_t destination = packet.to == 0 ? MESHTASTIC_BROADCAST_NODE : packet.to;
            bool ok = false;

            memcpy( text, packet.decoded.payload.bytes, text_len );
            text[ text_len ] = '\0';

            if ( text[ 0 ] ) {
                ok = meshtastic_service_send_text_to( text, destination, channel_slot );
            }
            meshtastic_ble_queue_status( packet.id, ok );
        }

        void meshtastic_ble_handle_mesh_packet( const meshtastic_MeshPacket &packet ) {
            if ( packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag ) {
                return;
            }

            switch( packet.decoded.portnum ) {
                case meshtastic_PortNum_TEXT_MESSAGE_APP:
                    meshtastic_ble_handle_text_packet( packet );
                    break;

                case meshtastic_PortNum_ADMIN_APP:
                    meshtastic_ble_handle_admin_payload(
                        packet.decoded.payload.bytes,
                        packet.decoded.payload.size
                    );
                    break;

                default:
                    log_i( "Meshtastic BLE ignored port %u", (unsigned)packet.decoded.portnum );
                    break;
            }
        }

        void meshtastic_ble_text_rx_callback(
            uint32_t from_node,
            uint32_t to_node,
            uint8_t channel_slot,
            uint32_t packet_id,
            int32_t rssi,
            float snr,
            const char *text
        ) {
            meshtastic_FromRadio from_radio;
            meshtastic_MeshPacket mesh_packet;
            const size_t text_len = text ? strlen( text ) : 0;
            const size_t payload_len = text_len <= sizeof( mesh_packet.decoded.payload.bytes )
                                           ? text_len
                                           : sizeof( mesh_packet.decoded.payload.bytes );

            if ( !text || text[ 0 ] == '\0' || !meshtastic_ble_is_connected() ) {
                return;
            }

            memset( &mesh_packet, 0, sizeof( mesh_packet ) );
            mesh_packet.from = from_node;
            mesh_packet.to = to_node == 0 ? MESHTASTIC_BROADCAST_NODE : to_node;
            mesh_packet.channel = channel_slot < MESHTASTIC_CHANNEL_SLOTS ? channel_slot : 0;
            mesh_packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            mesh_packet.id = packet_id;
            mesh_packet.rx_time = meshtastic_ble_current_time_seconds();
            mesh_packet.rx_snr = snr;
            mesh_packet.rx_rssi = rssi;

            if ( !meshtastic_ble_fill_data_payload(
                     mesh_packet.decoded,
                     meshtastic_PortNum_TEXT_MESSAGE_APP,
                     (const uint8_t *)text,
                     payload_len
                 ) ) {
                return;
            }

            mesh_packet.decoded.dest = mesh_packet.to;
            mesh_packet.decoded.source = mesh_packet.from;

            memset( &from_radio, 0, sizeof( from_radio ) );
            from_radio.which_payload_variant = meshtastic_FromRadio_packet_tag;
            from_radio.packet = mesh_packet;
            meshtastic_ble_enqueue_from_radio( from_radio );
        }

        class MeshtasticToRadioCallbacks : public NimBLECharacteristicCallbacks {
            void onWrite( NimBLECharacteristic *pCharacteristic ) {
                meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
                const std::string value = pCharacteristic->getValue();

                if ( value.empty() ) {
                    return;
                }

                if ( !meshtastic_ble_decode_message(
                         &meshtastic_ToRadio_msg,
                         &to_radio,
                         (const uint8_t *)value.data(),
                         value.length()
                     ) ) {
                    log_w( "Meshtastic BLE rejected malformed ToRadio payload" );
                    return;
                }

                switch( to_radio.which_payload_variant ) {
                    case meshtastic_ToRadio_want_config_id_tag:
                        meshtastic_ble_queue_config_sequence( to_radio.want_config_id );
                        break;

                    case meshtastic_ToRadio_packet_tag:
                        meshtastic_ble_handle_mesh_packet( to_radio.packet );
                        break;

                    case meshtastic_ToRadio_disconnect_tag:
                        meshtastic_ble_on_disconnect();
                        break;

                    case meshtastic_ToRadio_heartbeat_tag:
                        meshtastic_ble_queue_status( 0, true );
                        break;

                    default:
                        log_i( "Meshtastic BLE ignored ToRadio variant %u", (unsigned)to_radio.which_payload_variant );
                        break;
                }
            }
        };

        class MeshtasticFromRadioCallbacks : public NimBLECharacteristicCallbacks {
            void onRead( NimBLECharacteristic *pCharacteristic ) {
                meshtastic_ble_frame_t frame = { 0 };

                if ( meshtastic_from_radio_queue &&
                     xQueueReceive( meshtastic_from_radio_queue, &frame, 0 ) == pdTRUE ) {
                    pCharacteristic->setValue( frame.data, frame.len );
                } else {
                    pCharacteristic->setValue( meshtastic_ble_empty_value, 0 );
                }
            }
        };

        MeshtasticToRadioCallbacks meshtastic_to_radio_callbacks;
        MeshtasticFromRadioCallbacks meshtastic_from_radio_callbacks;
    }

    void meshtastic_ble_setup( void ) {
        NimBLEServer *server = blectl_get_ble_server();
        NimBLEService *service = NULL;
        bool pairing_enabled = false;

        if ( !server || meshtastic_to_radio_characteristic ) {
            return;
        }

        if ( !meshtastic_from_radio_queue ) {
            meshtastic_from_radio_queue = xQueueCreate(
                MESHTASTIC_FROMRADIO_QUEUE_DEPTH,
                sizeof( meshtastic_ble_frame_t )
            );
        }

        meshtastic_ble_reset_shadow_config();
        meshtastic_service_set_text_rx_callback( meshtastic_ble_text_rx_callback );
        pairing_enabled = meshtastic_ble_pairing_enabled();

        service = server->createService( NimBLEUUID( MESHTASTIC_BLE_SERVICE_UUID ) );
        meshtastic_to_radio_characteristic = service->createCharacteristic(
            NimBLEUUID( MESHTASTIC_BLE_TORADIO_UUID ),
            pairing_enabled
                ? ( NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC )
                : NIMBLE_PROPERTY::WRITE
        );
        meshtastic_from_radio_characteristic = service->createCharacteristic(
            NimBLEUUID( MESHTASTIC_BLE_FROMRADIO_UUID ),
            pairing_enabled
                ? ( NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC )
                : NIMBLE_PROPERTY::READ
        );
        meshtastic_from_num_characteristic = service->createCharacteristic(
            NimBLEUUID( MESHTASTIC_BLE_FROMNUM_UUID ),
            pairing_enabled
                ? ( NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ |
                    NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC )
                : ( NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ )
        );
        meshtastic_log_radio_characteristic = service->createCharacteristic(
            NimBLEUUID( MESHTASTIC_BLE_LOGRADIO_UUID ),
            pairing_enabled
                ? ( NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ |
                    NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC )
                : ( NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ ),
            512U
        );

        meshtastic_to_radio_characteristic->setCallbacks( &meshtastic_to_radio_callbacks );
        meshtastic_from_radio_characteristic->setCallbacks( &meshtastic_from_radio_callbacks );
        meshtastic_ble_set_from_num_value( 0 );
        meshtastic_log_radio_characteristic->setValue( meshtastic_ble_empty_value, 0 );

        service->start();
    }

    bool meshtastic_ble_configure_advertising( void ) {
        NimBLEAdvertising *advertising = blectl_get_ble_advertising();

        if ( !advertising || !meshtastic_to_radio_characteristic ) {
            return( false );
        }

        advertising->stop();
        advertising->reset();
        advertising->removeServices();
        advertising->setName( device_get_name() );
        advertising->addServiceUUID( MESHTASTIC_BLE_SERVICE_UUID );
        advertising->addServiceUUID( NimBLEUUID( MESHTASTIC_BATTERY_SERVICE_UUID ) );
        return( advertising->start( 0 ) );
    }

    void meshtastic_ble_on_disconnect( void ) {
        meshtastic_ble_clear_from_radio_queue();
        meshtastic_ble_from_radio_num = 0;
        meshtastic_ble_set_from_num_value( 0 );
    }

    bool meshtastic_ble_pairing_enabled( void ) {
        return(
            meshtastic_ble_config_store.bluetooth.enabled &&
            meshtastic_ble_config_store.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN
        );
    }

    bool meshtastic_ble_pairing_uses_fixed_pin( void ) {
        return(
            meshtastic_ble_pairing_enabled() &&
            meshtastic_ble_config_store.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN &&
            meshtastic_ble_config_store.bluetooth.fixed_pin > 0
        );
    }

    uint32_t meshtastic_ble_pairing_fixed_pin( void ) {
        if ( meshtastic_ble_pairing_uses_fixed_pin() ) {
            return( meshtastic_ble_config_store.bluetooth.fixed_pin );
        }
        return( 0 );
    }

#else

    void meshtastic_ble_setup( void ) {
    }

    bool meshtastic_ble_configure_advertising( void ) {
        return( false );
    }

    void meshtastic_ble_on_disconnect( void ) {
    }

    bool meshtastic_ble_pairing_enabled( void ) {
        return( false );
    }

    bool meshtastic_ble_pairing_uses_fixed_pin( void ) {
        return( false );
    }

    uint32_t meshtastic_ble_pairing_fixed_pin( void ) {
        return( 0 );
    }

#endif
