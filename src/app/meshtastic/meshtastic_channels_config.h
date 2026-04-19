#ifndef _MESHTASTIC_CHANNELS_CONFIG_H
    #define _MESHTASTIC_CHANNELS_CONFIG_H

    #include "utils/basejsonconfig.h"

    #define MESHTASTIC_CHANNELS_JSON_CONFIG_FILE "/meshtastic_channels.json"
    #define MESHTASTIC_CHANNEL_COUNT 8
    #define MESHTASTIC_CHANNEL_NAME_LEN 16
    #define MESHTASTIC_CHANNEL_PSK_B64_LEN 96

    typedef struct {
        bool enabled;
        char name[ MESHTASTIC_CHANNEL_NAME_LEN ];
        char psk[ MESHTASTIC_CHANNEL_PSK_B64_LEN ];
    } meshtastic_channel_config_entry_t;

    class meshtastic_channels_config_t : public BaseJsonConfig {
        public:
        meshtastic_channels_config_t();

        uint8_t active_channel = 0;
        meshtastic_channel_config_entry_t channels[ MESHTASTIC_CHANNEL_COUNT ];

        protected:
        virtual bool onLoad( JsonDocument& document );
        virtual bool onSave( JsonDocument& document );
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 4096; }
    };

#endif
