#ifndef _XNODE_H
    #define _XNODE_H

    void xnode_setup( void );
    void xnode_on_disconnect( void );
    const char *xnode_ble_service_uuid( void );
    bool xnode_send_meshtastic_rx( const char *from, const char *text );
    // Local/GPS updates may set the device's persisted SOS/check-in position.
    bool xnode_send_location_update( double lat, double lon, const char *label );
    // Peer notifications never change the local emergency position.
    bool xnode_send_peer_location( double lat, double lon, const char *label, const char *public_key = nullptr );
    bool xnode_send_manual_sos( void );
    bool xnode_send_manual_checkin( void );

#endif // _XNODE_H
