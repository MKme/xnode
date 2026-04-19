#ifndef _MESHTASTIC_BLE_H
    #define _MESHTASTIC_BLE_H
    #include <stdint.h>

    void meshtastic_ble_setup( void );
    bool meshtastic_ble_configure_advertising( void );
    void meshtastic_ble_on_disconnect( void );
    bool meshtastic_ble_pairing_enabled( void );
    bool meshtastic_ble_pairing_uses_fixed_pin( void );
    uint32_t meshtastic_ble_pairing_fixed_pin( void );

#endif
