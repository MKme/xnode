#ifndef _XNODE_H
    #define _XNODE_H

    void xnode_setup( void );
    bool xnode_send_meshtastic_rx( const char *from, const char *text );
    bool xnode_send_location_update( double lat, double lon, const char *label );

#endif // _XNODE_H
