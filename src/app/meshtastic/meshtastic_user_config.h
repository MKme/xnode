#ifndef _MESHTASTIC_USER_CONFIG_H
    #define _MESHTASTIC_USER_CONFIG_H

    #include "utils/basejsonconfig.h"

    #define MESHTASTIC_USER_JSON_CONFIG_FILE "/meshtastic_user.json"
    #define MESHTASTIC_USER_CONFIG_LONG_NAME_LEN 40
    #define MESHTASTIC_USER_CONFIG_SHORT_NAME_LEN 5

    class meshtastic_user_config_t : public BaseJsonConfig {
        public:
        meshtastic_user_config_t();

        char long_name[ MESHTASTIC_USER_CONFIG_LONG_NAME_LEN ] = "";
        char short_name[ MESHTASTIC_USER_CONFIG_SHORT_NAME_LEN ] = "";
        bool is_licensed = false;
        bool is_unmessageable = false;

        protected:
        virtual bool onLoad( JsonDocument& document );
        virtual bool onSave( JsonDocument& document );
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 384; }
    };

#endif
