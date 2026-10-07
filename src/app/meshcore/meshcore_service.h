#ifndef _MESHCORE_SERVICE_H
    #define _MESHCORE_SERVICE_H

    #include <stdbool.h>
    #include <stdint.h>
    #include "app/meshtastic/meshtastic_service.h"

    void meshcore_service_setup( void );
    bool meshcore_service_send_text( const char *text );
    bool meshcore_service_send_text_to( const char *text, uint32_t dest, uint8_t channel_slot );
    bool meshcore_service_is_ready( void );
    bool meshcore_service_is_receiving( void );
    const char *meshcore_service_get_status( void );
    uint8_t meshcore_service_get_channel_count( void );
    // UI dropdown indices are not channel slots when disabled slots are skipped.
    int8_t meshcore_service_get_channel_slot( uint8_t channel_index );
    uint32_t meshcore_service_get_history_revision( void );
    size_t meshcore_service_get_history_count( uint8_t channel_slot );
    bool meshcore_service_get_history_message( uint8_t channel_slot, size_t chronological_index, mesh_message_t *out );
    const char *meshcore_service_get_channel_name( uint8_t channel_index );
    uint8_t meshcore_service_get_active_channel( void );
    bool meshcore_service_set_active_channel( uint8_t channel_index );
    const char *meshcore_service_get_active_channel_name( void );
    const char *meshcore_service_get_primary_channel_name( void );
    float meshcore_service_get_frequency_mhz( void );
    uint32_t meshcore_service_get_node_id( void );
    const char *meshcore_service_get_long_name( void );
    const char *meshcore_service_get_short_name( void );
    bool meshcore_service_get_user_info( meshtastic_service_user_info_t *info );
    bool meshcore_service_set_user_info( const meshtastic_service_user_info_t *info );
    bool meshcore_service_broadcast_node_info( void );
    void meshcore_service_schedule_node_info_broadcast( uint32_t delay_ms );
    uint32_t meshcore_service_get_last_peer( void );
    int32_t meshcore_service_get_last_rssi( void );
    float meshcore_service_get_last_snr( void );
    const char *meshcore_service_get_last_message_sender( void );
    const char *meshcore_service_get_last_message_text( void );
    bool meshcore_service_get_channel_info( uint8_t channel_slot, meshtastic_service_channel_info_t *info );
    bool meshcore_service_set_channel_info( uint8_t channel_slot, const meshtastic_service_channel_info_t *info );
    void meshcore_service_set_text_rx_callback( meshtastic_service_text_rx_cb_t callback );

    // Full Ed25519 public identity. Legacy node IDs are local UI handles only.
    const char *meshcore_service_get_public_key_hex(void);
    struct meshcore_service_radio_config_t {
        float frequency_mhz;
        float bandwidth_khz;
        uint8_t spreading_factor;
        uint8_t coding_rate;
        int8_t tx_power_dbm;
    };
    bool meshcore_service_get_radio_config(meshcore_service_radio_config_t *out);
    bool meshcore_service_get_active_radio_config(meshcore_service_radio_config_t *out);
    bool meshcore_service_radio_reboot_required(void);
    // Validates and saves for NEXT BOOT; never changes a live modem mid-packet.
    bool meshcore_service_set_radio_config(const meshcore_service_radio_config_t *config);
#endif
