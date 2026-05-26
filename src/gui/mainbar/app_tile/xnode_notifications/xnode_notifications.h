#ifndef _XNODE_NOTIFICATIONS_H
    #define _XNODE_NOTIFICATIONS_H

    #include <stdint.h>

    void xnode_notifications_tile_setup( void );
    bool xnode_notifications_push( const char *source, const char *title, const char *body, uint32_t ts );
    void xnode_notifications_set_enabled( bool enabled );
    bool xnode_notifications_get_enabled( void );
    uint32_t xnode_notifications_get_count( void );

#endif // _XNODE_NOTIFICATIONS_H
