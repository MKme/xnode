#ifndef _MESHTASTIC_SERVICE_H
    #define _MESHTASTIC_SERVICE_H

    #include <stdbool.h>
    #include <stdint.h>

    #define MESHTASTIC_SERVICE_MAX_CHANNEL_NAME_LEN 16
    #define MESHTASTIC_SERVICE_MAX_PSK_LEN 32
    #define MESHTASTIC_SERVICE_LONG_NAME_LEN 40
    #define MESHTASTIC_SERVICE_SHORT_NAME_LEN 5
    #define MESHTASTIC_SERVICE_CHANNEL_ROLE_DISABLED 0
    #define MESHTASTIC_SERVICE_CHANNEL_ROLE_PRIMARY 1
    #define MESHTASTIC_SERVICE_CHANNEL_ROLE_SECONDARY 2

    typedef struct {
        char long_name[ MESHTASTIC_SERVICE_LONG_NAME_LEN ];
        char short_name[ MESHTASTIC_SERVICE_SHORT_NAME_LEN ];
        bool is_licensed;
        bool is_unmessageable;
    } meshtastic_service_user_info_t;

    typedef struct {
        bool enabled;
        uint8_t role;
        char name[ MESHTASTIC_SERVICE_MAX_CHANNEL_NAME_LEN ];
        uint8_t psk_len;
        uint8_t psk[ MESHTASTIC_SERVICE_MAX_PSK_LEN ];
    } meshtastic_service_channel_info_t;

    typedef void ( *meshtastic_service_text_rx_cb_t )(
        uint32_t from_node,
        uint32_t to_node,
        uint8_t channel_slot,
        uint32_t packet_id,
        int32_t rssi,
        float snr,
        const char *text
    );

    void meshtastic_service_setup( void );
    bool meshtastic_service_send_text( const char *text );
    bool meshtastic_service_send_text_to( const char *text, uint32_t dest, uint8_t channel_slot );
    bool meshtastic_service_is_ready( void );
    bool meshtastic_service_is_receiving( void );
    const char *meshtastic_service_get_status( void );
    uint8_t meshtastic_service_get_channel_count( void );
    const char *meshtastic_service_get_channel_name( uint8_t channel_index );
    uint8_t meshtastic_service_get_active_channel( void );
    bool meshtastic_service_set_active_channel( uint8_t channel_index );
    const char *meshtastic_service_get_active_channel_name( void );
    const char *meshtastic_service_get_primary_channel_name( void );
    float meshtastic_service_get_frequency_mhz( void );
    uint32_t meshtastic_service_get_node_id( void );
    const char *meshtastic_service_get_long_name( void );
    const char *meshtastic_service_get_short_name( void );
    bool meshtastic_service_get_user_info( meshtastic_service_user_info_t *info );
    bool meshtastic_service_set_user_info( const meshtastic_service_user_info_t *info );
    bool meshtastic_service_broadcast_node_info( void );
    void meshtastic_service_schedule_node_info_broadcast( uint32_t delay_ms );
    uint32_t meshtastic_service_get_last_peer( void );
    int32_t meshtastic_service_get_last_rssi( void );
    float meshtastic_service_get_last_snr( void );
    const char *meshtastic_service_get_last_message_sender( void );
    const char *meshtastic_service_get_last_message_text( void );
    bool meshtastic_service_get_channel_info( uint8_t channel_slot, meshtastic_service_channel_info_t *info );
    bool meshtastic_service_set_channel_info( uint8_t channel_slot, const meshtastic_service_channel_info_t *info );
    void meshtastic_service_set_text_rx_callback( meshtastic_service_text_rx_cb_t callback );

#endif
