#ifndef _XNODE_NOTIFICATIONS_CONFIG_H
    #define _XNODE_NOTIFICATIONS_CONFIG_H

    #include <stdint.h>

    #include "utils/basejsonconfig.h"

    #define XNODE_NOTIFICATIONS_JSON_CONFIG_FILE "/xnode_notifications.json"
    #define XNODE_NOTIFICATIONS_HISTORY_MAX      30
    #define XNODE_NOTIFICATIONS_SOURCE_LEN       24
    #define XNODE_NOTIFICATIONS_TITLE_LEN        56
    #define XNODE_NOTIFICATIONS_BODY_LEN         160

    typedef struct {
        uint32_t ts;
        uint32_t id;
        char source[ XNODE_NOTIFICATIONS_SOURCE_LEN ];
        char title[ XNODE_NOTIFICATIONS_TITLE_LEN ];
        char body[ XNODE_NOTIFICATIONS_BODY_LEN ];
    } xnode_notification_entry_t;

    class xnode_notifications_config_t : public BaseJsonConfig {
        public:
            xnode_notifications_config_t();
            bool enabled = true;
            uint8_t history_limit = XNODE_NOTIFICATIONS_HISTORY_MAX;
            uint8_t history_count = 0;
            xnode_notification_entry_t history[ XNODE_NOTIFICATIONS_HISTORY_MAX ];

        protected:
            virtual bool onLoad( JsonDocument& document );
            virtual bool onSave( JsonDocument& document );
            virtual bool onDefault( void );
            virtual size_t getJsonBufferSize() { return 16384; }
    };

#endif // _XNODE_NOTIFICATIONS_CONFIG_H
