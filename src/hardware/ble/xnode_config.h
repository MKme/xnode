#ifndef _XNODE_CONFIG_H
    #define _XNODE_CONFIG_H

    #include <stdint.h>

    #include "utils/basejsonconfig.h"

    #define XNODE_JSON_CONFIG_FILE "/xnode.json"

    class xnode_config_t : public BaseJsonConfig {
        public:
        xnode_config_t();

        uint16_t watch_unit_id = 0;
        uint16_t sos_to_unit_id = 0;
        char watch_unit_label[ 32 ] = "";
        bool has_location = false;
        double lat = 0.0;
        double lon = 0.0;

        protected:
        virtual bool onLoad( JsonDocument& document );
        virtual bool onSave( JsonDocument& document );
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 768; }
    };

#endif // _XNODE_CONFIG_H
