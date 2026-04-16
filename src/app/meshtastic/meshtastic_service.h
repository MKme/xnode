#ifndef _MESHTASTIC_SERVICE_H
    #define _MESHTASTIC_SERVICE_H

    #include <stdbool.h>
    #include <stdint.h>

    void meshtastic_service_setup( void );
    bool meshtastic_service_send_text( const char *text );
    bool meshtastic_service_is_ready( void );
    bool meshtastic_service_is_receiving( void );
    const char *meshtastic_service_get_status( void );
    uint32_t meshtastic_service_get_node_id( void );
    uint32_t meshtastic_service_get_last_peer( void );
    int32_t meshtastic_service_get_last_rssi( void );
    float meshtastic_service_get_last_snr( void );

#endif
