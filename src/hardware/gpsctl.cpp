/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
 ****************************************************************************/
 
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "config.h"
#include "gpsctl.h"
#include "timesync.h"
#include "powermgm.h"
#include "callback.h"
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include "utils/millis.h"

    #define DEG_TO_RAD 0.017453292519943295769236907684886
    #define RAD_TO_DEG 57.295779513082320876798154814105

    #define radians(deg) ((deg)*DEG_TO_RAD)
    #define degrees(rad) ((rad)*RAD_TO_DEG)

#else
    #if defined( M5PAPER )
        #include <M5EPD.h>
    #elif defined( M5CORE2 )
        #include <M5Core2.h>
    #elif defined( LILYGO_WATCH_ULTRA )
        #include "hardware/twatch_ultra_hal.h"
    #elif defined( LILYGO_T_DECK_PLUS )
        #include "hardware/tdeck_plus_hal.h"
    #elif defined( LILYGO_WATCH_S3 )
        #include <LilyGoLib.h>
    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        #include <TTGO.h>
    #endif

    #include <TinyGPS++.h>

    static const uint32_t GPSBaud = 9600;

    TinyGPSPlus gps;
    TinyGPSCustom TGC_sats_in_view_gps;
    TinyGPSCustom TGC_sats_in_view_glonass;
    TinyGPSCustom TGC_sats_in_view_baidou;

    #if defined( USE_SOFTWARE_SERIAL )
        #include <SoftwareSerial.h>
        SoftwareSerial *gps_serial = NULL;
    #else
        HardwareSerial *gps_serial = NULL;
    #endif
#endif

static bool gpsctl_init = false;
static bool gpsctl_enable = false;
static bool gpsctl_probe_done = false;
static bool gpsctl_probe_ok = false;
static uint32_t gpsctl_active_baud = 0;
static char gpsctl_probe_model[12] = "unknown";
static char gpsctl_last_sentence[12] = "";
static char gpsctl_sentence_token[12] = "";
static uint8_t gpsctl_sentence_token_len = 0;
static bool gpsctl_sentence_token_active = false;
static uint32_t gpsctl_rx_bytes = 0;
static uint32_t gpsctl_last_rx_millis = 0;
static uint32_t gpsctl_last_sentence_millis = 0;
static uint32_t gpsctl_last_sentence_total = 0;
static uint32_t gpsctl_time_sync_count = 0;
static time_t gpsctl_last_time_sync_epoch = 0;
static uint32_t gpsctl_last_time_sync_millis = 0;

gpsctl_config_t gpsctl_config;
callback_t *gpsctl_callback = NULL;
gps_data_t gps_data;

bool gpsctl_powermgm_loop_cb( EventBits_t event, void *arg );
bool gpsctl_powermgm_event_cb( EventBits_t event, void *arg );
bool gpsctl_send_cb( EventBits_t event, void *arg );
void gpsctl_autoon_on( void );
void gpsctl_autoon_off( void );
static uint32_t gpsctl_get_configured_baud( void );
static void gpsctl_reset_debug_counters( void );

#ifndef NATIVE_64BIT
static void gpsctl_begin_serial( uint32_t baud );
static void gpsctl_note_serial_byte( int c );
static int gpsctl_read_serial_byte( void );
static void gpsctl_prepare_receiver_after_power_on( void );
static time_t gpsctl_get_gps_epoch_utc( void );
static void gpsctl_sync_time_from_gps( void );
#endif

static uint32_t gpsctl_get_configured_baud( void ) {
#ifdef NATIVE_64BIT
    return( 0 );
#elif defined( LILYGO_WATCH_ULTRA )
    return( BOARD_GPS_BAUDRATE );
#elif defined( LILYGO_T_DECK_PLUS )
    return( BOARD_GPS_BAUDRATE );
#elif defined( LILYGO_WATCH_S3 )
    return( 38400 );
#else
    return( GPSBaud );
#endif
}

static void gpsctl_reset_debug_counters( void ) {
    gpsctl_probe_done = false;
    gpsctl_probe_ok = false;
    gpsctl_active_baud = 0;
    snprintf( gpsctl_probe_model, sizeof( gpsctl_probe_model ), "unknown" );
    gpsctl_rx_bytes = 0;
    gpsctl_last_rx_millis = 0;
    gpsctl_last_sentence_millis = 0;
    gpsctl_last_sentence[0] = '\0';
    gpsctl_sentence_token[0] = '\0';
    gpsctl_sentence_token_len = 0;
    gpsctl_sentence_token_active = false;
#ifdef NATIVE_64BIT
    gpsctl_last_sentence_total = 0;
#else
    gpsctl_last_sentence_total = gps.passedChecksum() + gps.failedChecksum();
#endif
}

#ifndef NATIVE_64BIT
static void gpsctl_note_serial_byte( int c ) {
    if ( c == '$' ) {
        gpsctl_sentence_token_active = true;
        gpsctl_sentence_token_len = 0;
        gpsctl_sentence_token[gpsctl_sentence_token_len++] = '$';
        gpsctl_sentence_token[gpsctl_sentence_token_len] = '\0';
        return;
    }

    if ( !gpsctl_sentence_token_active ) {
        return;
    }

    if ( c == ',' || c == '\r' || c == '\n' ) {
        gpsctl_sentence_token[gpsctl_sentence_token_len] = '\0';
        if ( gpsctl_sentence_token_len > 1 ) {
            snprintf( gpsctl_last_sentence, sizeof( gpsctl_last_sentence ), "%s", gpsctl_sentence_token );
        }
        gpsctl_sentence_token_active = false;
        gpsctl_sentence_token_len = 0;
        return;
    }

    if ( gpsctl_sentence_token_len < sizeof( gpsctl_sentence_token ) - 1 ) {
        gpsctl_sentence_token[gpsctl_sentence_token_len++] = (char)c;
        gpsctl_sentence_token[gpsctl_sentence_token_len] = '\0';
    }
    else {
        gpsctl_sentence_token_active = false;
        gpsctl_sentence_token_len = 0;
    }
}

static void gpsctl_begin_serial( uint32_t baud ) {
    if ( !gps_serial ) {
        return;
    }

    gpsctl_active_baud = baud;
#if defined( USE_SOFTWARE_SERIAL )
    gps_serial->begin( GPSBaud );
    gpsctl_active_baud = GPSBaud;
#else
    gps_serial->begin( baud, SERIAL_8N1, gpsctl_config.RXPin, gpsctl_config.TXPin );
#endif
}

static int gpsctl_read_serial_byte( void ) {
    if ( !gps_serial ) {
        return( -1 );
    }

    int c = gps_serial->read();
    if ( c >= 0 ) {
        gpsctl_rx_bytes++;
        gpsctl_last_rx_millis = millis();
        gpsctl_note_serial_byte( c );
    }
    return( c );
}

#if ( defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS ) ) && !defined( USE_SOFTWARE_SERIAL )
static void gpsctl_set_probe_model( const char *model ) {
    if ( !model ) {
        model = "unknown";
    }
    snprintf( gpsctl_probe_model, sizeof( gpsctl_probe_model ), "%s", model );
}

#if defined( LILYGO_WATCH_ULTRA )
static bool gpsctl_wait_for_ubx( uint8_t requested_class, uint8_t requested_id, uint32_t timeout_ms ) {
    uint8_t state = 0;
    uint16_t payload_len = 0;
    uint16_t bytes_to_skip = 0;
    uint32_t start = millis();

    while ( (uint32_t)( millis() - start ) < timeout_ms ) {
        while ( gps_serial && gps_serial->available() > 0 ) {
            int c = gpsctl_read_serial_byte();
            if ( c < 0 ) {
                continue;
            }

            switch ( state ) {
                case 0:
                    state = ( c == 0xB5 ) ? 1 : 0;
                    break;
                case 1:
                    state = ( c == 0x62 ) ? 2 : 0;
                    break;
                case 2:
                    state = ( c == requested_class ) ? 3 : 0;
                    break;
                case 3:
                    state = ( c == requested_id ) ? 4 : 0;
                    break;
                case 4:
                    payload_len = (uint8_t)c;
                    state = 5;
                    break;
                case 5:
                    payload_len |= ( (uint16_t)(uint8_t)c << 8 );
                    if ( payload_len > 512 ) {
                        state = 0;
                    }
                    else {
                        bytes_to_skip = payload_len + 2;
                        state = ( bytes_to_skip == 0 ) ? 0 : 6;
                    }
                    break;
                case 6:
                    if ( bytes_to_skip > 0 ) {
                        bytes_to_skip--;
                    }
                    if ( bytes_to_skip == 0 ) {
                        return( true );
                    }
                    break;
                default:
                    state = 0;
                    break;
            }
        }
        delay( 2 );
    }
    return( false );
}

static bool gpsctl_wait_for_ls550g_version( uint32_t timeout_ms ) {
    char line[128] = "";
    size_t line_len = 0;
    uint32_t start = millis();

    while ( (uint32_t)( millis() - start ) < timeout_ms ) {
        while ( gps_serial && gps_serial->available() > 0 ) {
            int c = gpsctl_read_serial_byte();
            if ( c < 0 ) {
                continue;
            }

            if ( c == '\r' ) {
                continue;
            }
            if ( c == '\n' ) {
                line[line_len] = '\0';
                if ( strstr( line, "$PQTMQVER,OK,1,MODULE,LS550G" ) != NULL ) {
                    return( true );
                }
                line_len = 0;
                continue;
            }
            if ( line_len < sizeof( line ) - 1 ) {
                line[line_len++] = (char)c;
            }
            else {
                line_len = 0;
            }
        }
        delay( 2 );
    }

    line[line_len] = '\0';
    return( strstr( line, "$PQTMQVER,OK,1,MODULE,LS550G" ) != NULL );
}
#endif

static void gpsctl_drain_serial_for( uint32_t duration_ms ) {
    uint32_t start = millis();
    while ( (uint32_t)( millis() - start ) < duration_ms ) {
        bool drained = false;
        while ( gps_serial && gps_serial->available() > 0 ) {
            gpsctl_read_serial_byte();
            drained = true;
        }
        if ( !drained ) {
            delay( 2 );
        }
    }
}

#if defined( LILYGO_T_DECK_PLUS )
static bool gpsctl_wait_for_tdeck_ubx( uint8_t requested_class, uint8_t requested_id, uint32_t timeout_ms ) {
    uint8_t state = 0;
    uint16_t payload_len = 0;
    uint16_t bytes_to_skip = 0;
    uint32_t start = millis();

    while ( (uint32_t)( millis() - start ) < timeout_ms ) {
        while ( gps_serial && gps_serial->available() > 0 ) {
            int c = gpsctl_read_serial_byte();
            if ( c < 0 ) {
                continue;
            }

            switch ( state ) {
                case 0:
                    state = ( c == 0xB5 ) ? 1 : 0;
                    break;
                case 1:
                    state = ( c == 0x62 ) ? 2 : 0;
                    break;
                case 2:
                    state = ( c == requested_class ) ? 3 : 0;
                    break;
                case 3:
                    state = ( c == requested_id ) ? 4 : 0;
                    break;
                case 4:
                    payload_len = (uint8_t)c;
                    state = 5;
                    break;
                case 5:
                    payload_len |= ( (uint16_t)(uint8_t)c << 8 );
                    if ( payload_len > 512 ) {
                        state = 0;
                    }
                    else {
                        bytes_to_skip = payload_len + 2;
                        state = ( bytes_to_skip == 0 ) ? 0 : 6;
                    }
                    break;
                case 6:
                    if ( bytes_to_skip > 0 ) {
                        bytes_to_skip--;
                    }
                    if ( bytes_to_skip == 0 ) {
                        return( true );
                    }
                    break;
                default:
                    state = 0;
                    break;
            }
        }
        delay( 2 );
    }

    return( false );
}

static bool gpsctl_wait_for_l76k_version( uint32_t timeout_ms ) {
    char line[128] = "";
    size_t line_len = 0;
    uint32_t start = millis();

    while ( (uint32_t)( millis() - start ) < timeout_ms ) {
        while ( gps_serial && gps_serial->available() > 0 ) {
            int c = gpsctl_read_serial_byte();
            if ( c < 0 ) {
                continue;
            }

            if ( c == '\r' ) {
                continue;
            }
            if ( c == '\n' ) {
                line[line_len] = '\0';
                if ( strstr( line, "$GPTXT,01,01,02" ) != NULL || strstr( line, "L76K" ) != NULL ) {
                    return( true );
                }
                line_len = 0;
                continue;
            }
            if ( line_len < sizeof( line ) - 1 ) {
                line[line_len++] = (char)c;
            }
            else {
                line_len = 0;
            }
        }
        delay( 2 );
    }

    line[line_len] = '\0';
    return( strstr( line, "$GPTXT,01,01,02" ) != NULL || strstr( line, "L76K" ) != NULL );
}

static bool gpsctl_probe_tdeck_l76k( void ) {
    const uint32_t start_rx_bytes = gpsctl_rx_bytes;
    gpsctl_begin_serial( BOARD_GPS_BAUDRATE );
    delay( 100 );

    gps_serial->write( "$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n" );
    gps_serial->flush();
    delay( 100 );
    gpsctl_drain_serial_for( 100 );

    gps_serial->write( "$PCAS06,0*1B\r\n" );
    gps_serial->flush();
    const bool saw_l76k_version = gpsctl_wait_for_l76k_version( 900 );

    gps_serial->write( "$PCAS04,5*1C\r\n" );
    delay( 20 );
    gps_serial->write( "$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n" );
    delay( 20 );
    gps_serial->write( "$PCAS11,3*1E\r\n" );
    gps_serial->flush();

    if ( saw_l76k_version ) {
        gpsctl_set_probe_model( "L76K" );
        return( true );
    }

    if ( gpsctl_rx_bytes > start_rx_bytes ) {
        gpsctl_set_probe_model( "nmea" );
        return( true );
    }

    gpsctl_set_probe_model( "silent" );
    return( false );
}

static bool gpsctl_probe_tdeck_ublox_baud( uint32_t baud, bool *saw_rx ) {
    static const uint8_t cfg_rate_poll[] = { 0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30 };
    const uint32_t start_rx_bytes = gpsctl_rx_bytes;

    if ( saw_rx ) {
        *saw_rx = false;
    }

    gpsctl_begin_serial( baud );
    delay( 120 );
    gpsctl_drain_serial_for( 150 );

    gps_serial->write( cfg_rate_poll, sizeof( cfg_rate_poll ) );
    gps_serial->flush();
    const bool ok = gpsctl_wait_for_tdeck_ubx( 0x06, 0x08, 900 );
    if ( saw_rx ) {
        *saw_rx = gpsctl_rx_bytes > start_rx_bytes;
    }
    return( ok );
}
#endif

#if defined( LILYGO_WATCH_ULTRA )
static bool gpsctl_probe_ultra_baud( uint32_t baud, bool *saw_rx ) {
    static const uint8_t cfg_get_hw[] = { 0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34 };
    const uint32_t start_rx_bytes = gpsctl_rx_bytes;

    if ( saw_rx ) {
        *saw_rx = false;
    }

    gpsctl_begin_serial( baud );
    delay( 50 );
    gpsctl_drain_serial_for( 100 );

    gps_serial->write( cfg_get_hw, sizeof( cfg_get_hw ) );
    gps_serial->flush();
    bool ok = gpsctl_wait_for_ubx( 0x0A, 0x04, 800 );
    if ( saw_rx ) {
        *saw_rx = gpsctl_rx_bytes > start_rx_bytes;
    }
    return( ok );
}

static bool gpsctl_probe_ls550g( void ) {
    gpsctl_begin_serial( 115200 );
    delay( 50 );
    gpsctl_drain_serial_for( 100 );

    gps_serial->write( "$PQTMGNSSSTOP*09\r\n" );
    gps_serial->flush();
    delay( 250 );
    while ( gps_serial && gps_serial->available() > 0 ) {
        gpsctl_read_serial_byte();
    }

    gps_serial->write( "$PQTMQVER*08\r\n" );
    gps_serial->flush();
    if ( !gpsctl_wait_for_ls550g_version( 700 ) ) {
        return( false );
    }

    gps_serial->write( "$PQTMCFGCNST,W,1,1,1,1,0,0*2B\r\n" );
    delay( 50 );
    gps_serial->write( "$PQTMCFGPPS,W,1,1,100,1,1,0*73\r\n" );
    delay( 50 );
    gps_serial->write( "$PQTMGNSSSTART*51\r\n" );
    gps_serial->flush();
    return( true );
}
#endif
#endif

static void gpsctl_prepare_receiver_after_power_on( void ) {
    if ( !gps_serial ) {
        return;
    }

#if defined( LILYGO_WATCH_ULTRA ) && !defined( USE_SOFTWARE_SERIAL )
    const uint32_t probe_bauds[] = { BOARD_GPS_BAUDRATE, 57600, 115200, 9600 };
    uint32_t nmea_baud = 0;

    delay( 750 );
    gpsctl_probe_done = true;
    gpsctl_probe_ok = false;
    gpsctl_set_probe_model( "unknown" );

    for ( size_t i = 0; i < sizeof( probe_bauds ) / sizeof( probe_bauds[0] ); i++ ) {
        bool saw_rx = false;
        if ( gpsctl_probe_ultra_baud( probe_bauds[i], &saw_rx ) ) {
            gpsctl_probe_ok = true;
            gpsctl_set_probe_model( "ublox" );
            GPSCTL_INFO_LOG( "T-Watch Ultra GPS probe ok at %lu baud", (unsigned long)probe_bauds[i] );
            return;
        }
        if ( saw_rx && nmea_baud == 0 ) {
            nmea_baud = probe_bauds[i];
        }
    }

    if ( gpsctl_probe_ls550g() ) {
        gpsctl_probe_ok = true;
        gpsctl_set_probe_model( "LS550G" );
        GPSCTL_INFO_LOG( "T-Watch Ultra GPS probe ok as LS550G at 115200 baud" );
        return;
    }
    if ( nmea_baud != 0 ) {
        gpsctl_begin_serial( nmea_baud );
        gpsctl_probe_ok = true;
        gpsctl_set_probe_model( "nmea" );
        GPSCTL_INFO_LOG( "T-Watch Ultra GPS raw NMEA detected at %lu baud", (unsigned long)nmea_baud );
        return;
    }

    gpsctl_begin_serial( BOARD_GPS_BAUDRATE );
    GPSCTL_ERROR_LOG( "T-Watch Ultra GPS probe failed" );
#elif defined( LILYGO_T_DECK_PLUS ) && !defined( USE_SOFTWARE_SERIAL )
    gpsctl_probe_done = true;
    gpsctl_probe_ok = false;
    gpsctl_set_probe_model( "unknown" );

    if ( gpsctl_probe_tdeck_l76k() ) {
        gpsctl_probe_ok = true;
        GPSCTL_INFO_LOG( "T-Deck Plus GPS probe ok as %s at %lu baud", gpsctl_probe_model, (unsigned long)gpsctl_active_baud );
        return;
    }

    const uint32_t probe_bauds[] = { 38400, BOARD_GPS_BAUDRATE };
    uint32_t nmea_baud = 0;

    for ( size_t i = 0; i < sizeof( probe_bauds ) / sizeof( probe_bauds[0] ); i++ ) {
        bool saw_rx = false;
        if ( gpsctl_probe_tdeck_ublox_baud( probe_bauds[i], &saw_rx ) ) {
            gpsctl_probe_ok = true;
            gpsctl_set_probe_model( "M10" );
            GPSCTL_INFO_LOG( "T-Deck Plus GPS probe ok as u-blox/M10 at %lu baud", (unsigned long)probe_bauds[i] );
            return;
        }
        if ( saw_rx && nmea_baud == 0 ) {
            nmea_baud = probe_bauds[i];
        }
    }

    if ( nmea_baud != 0 ) {
        gpsctl_begin_serial( nmea_baud );
        gpsctl_probe_ok = true;
        gpsctl_set_probe_model( "nmea" );
        GPSCTL_INFO_LOG( "T-Deck Plus GPS raw NMEA detected at %lu baud", (unsigned long)nmea_baud );
        return;
    }

    GPSCTL_ERROR_LOG( "T-Deck Plus GPS probe failed at %lu baud", (unsigned long)gpsctl_active_baud );
#else
    gpsctl_begin_serial( gpsctl_get_configured_baud() );
#endif
}
#endif

void gpsctl_setup( void ) {
    /*
     * check if gpsctl already init
     */
    if ( gpsctl_init ) {
        return;
    }
    /*
     * load config from json
     */
    gpsctl_config.load();
    #if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )
        bool config_changed = false;
        if ( gpsctl_config.config_version < GPSCTL_CONFIG_VERSION ) {
            #if defined( LILYGO_T_DECK_PLUS )
                gpsctl_config.autoon = true;
                gpsctl_config.app_use_gps = true;
            #else
                gpsctl_config.autoon = false;
            #endif
            gpsctl_config.enable_on_standby = false;
            gpsctl_config.config_version = GPSCTL_CONFIG_VERSION;
            config_changed = true;
        }
        if ( gpsctl_config.RXPin != SHIELD_GPS_RX || gpsctl_config.TXPin != SHIELD_GPS_TX ) {
            gpsctl_config.RXPin = SHIELD_GPS_RX;
            gpsctl_config.TXPin = SHIELD_GPS_TX;
            config_changed = true;
        }
        #if defined( LILYGO_T_DECK_PLUS )
            if ( !gpsctl_config.autoon ) {
                gpsctl_config.autoon = true;
                config_changed = true;
            }
            if ( !gpsctl_config.app_use_gps ) {
                gpsctl_config.app_use_gps = true;
                config_changed = true;
            }
        #endif
        if ( config_changed ) {
            gpsctl_config.save();
        }
    #endif

    #ifdef NATIVE_64BIT

    #else
        /**
         * check if pin config valid
         */
        if( gpsctl_config.RXPin <= 0 || gpsctl_config.TXPin <= 0 ) {
            /**
             * load default pin settings for PORT.A if no pins defined
             */
            #if defined( M5PAPER )
                gpsctl_config.RXPin = GPIO_NUM_32;
                gpsctl_config.TXPin = GPIO_NUM_25;
            #elif defined( M5CORE2 )
                gpsctl_config.RXPin = GPIO_NUM_33;
                gpsctl_config.TXPin = GPIO_NUM_32;
            #elif defined( LILYGO_WATCH_ULTRA )
                gpsctl_config.RXPin = SHIELD_GPS_RX;
                gpsctl_config.TXPin = SHIELD_GPS_TX;
            #elif defined( LILYGO_T_DECK_PLUS )
                gpsctl_config.RXPin = SHIELD_GPS_RX;
                gpsctl_config.TXPin = SHIELD_GPS_TX;
            #elif defined( LILYGO_WATCH_S3 )
                gpsctl_config.RXPin = SHIELD_GPS_RX;
                gpsctl_config.TXPin = SHIELD_GPS_TX;
            #elif defined( LILYGO_WATCH_2020_V2 )
                gpsctl_config.RXPin = GPIO_NUM_36;
                gpsctl_config.TXPin = GPIO_NUM_26;
            #else
                gpsctl_config.RXPin = -1;
                gpsctl_config.TXPin = -1;
            #endif            
            gpsctl_config.save();
            GPSCTL_ERROR_LOG("set default gps RX on pin %d/TX on pin %d!", gpsctl_config.RXPin, gpsctl_config.TXPin );
        }
        /**
         * init tinyGPS++ if we have a valid RX/TX config
         */
        if( gpsctl_config.RXPin > 0 && gpsctl_config.TXPin > 0 ) {
            
            #if defined( USE_SOFTWARE_SERIAL )
                gps_serial = new SoftwareSerial( gpsctl_config.RXPin, gpsctl_config.TXPin );
                gpsctl_begin_serial( GPSBaud );
            #else
                #if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )
                    gps_serial = &Serial1;
                #else
                    gps_serial = &Serial2;
                #endif
                gpsctl_begin_serial( gpsctl_get_configured_baud() );
            #endif

            TGC_sats_in_view_gps.begin( gps, "GPGSV", 3);
            TGC_sats_in_view_glonass.begin( gps, "GLGSV", 3);
            TGC_sats_in_view_baidou.begin( gps, "BDGSV", 3);
        }
    #endif
    /**
     * register powermgm call back routine
     */
    powermgm_register_cb( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP, gpsctl_powermgm_event_cb, "powermgm gpsctl" );
    powermgm_register_loop_cb( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP, gpsctl_powermgm_loop_cb, "powermgm gpsctl loop" );

    gpsctl_init = true;

    gpsctl_send_cb( GPSCTL_UPDATE_CONFIG, NULL );
    gpsctl_autoon_on();
}

bool gpsctl_get_available( void ) {
    #ifdef NATIVE_64BIT
        return( false );
    #else
        if ( gps_serial ) {
            return( true );
        }
        else {
            return( false );
        }
    #endif
}

void gpsctl_get_debug( gpsctl_debug_t *debug ) {
    if ( !debug ) {
        return;
    }

    memset( debug, 0, sizeof( gpsctl_debug_t ) );
    debug->init = gpsctl_init;
    debug->enabled = gpsctl_enable;
    debug->rx_pin = gpsctl_config.RXPin;
    debug->tx_pin = gpsctl_config.TXPin;
    debug->baud = gpsctl_get_configured_baud();
    debug->active_baud = gpsctl_active_baud;
    debug->probe_done = gpsctl_probe_done;
    debug->probe_ok = gpsctl_probe_ok;
    snprintf( debug->probe_model, sizeof( debug->probe_model ), "%s", gpsctl_probe_model );
    snprintf( debug->last_sentence, sizeof( debug->last_sentence ), "%s", gpsctl_last_sentence );
    debug->valid_location = gps_data.valid_location;
    debug->valid_satellite = gps_data.valid_satellite;
    debug->satellites = gps_data.satellites;
    debug->gps_satellites = gps_data.satellite_types.gps_satellites;
    debug->glonass_satellites = gps_data.satellite_types.glonass_satellites;
    debug->baidou_satellites = gps_data.satellite_types.baidou_satellites;
    debug->lat = gps_data.lat;
    debug->lon = gps_data.lon;

    const uint32_t now = millis();
    debug->rx_bytes = gpsctl_rx_bytes;
    debug->last_rx_age_ms = gpsctl_last_rx_millis == 0 ? UINT32_MAX : now - gpsctl_last_rx_millis;
    debug->last_sentence_age_ms = gpsctl_last_sentence_millis == 0 ? UINT32_MAX : now - gpsctl_last_sentence_millis;

#ifdef NATIVE_64BIT
    debug->serial_available = false;
    debug->chars_processed = 0;
    debug->passed_checksum = 0;
    debug->failed_checksum = 0;
    debug->sentences_with_fix = 0;
    debug->location_age_ms = UINT32_MAX;
    debug->valid_date = false;
    debug->valid_time = false;
#else
    debug->serial_available = gps_serial != NULL;
    if ( gps_serial ) {
        debug->chars_processed = gps.charsProcessed();
        debug->passed_checksum = gps.passedChecksum();
        debug->failed_checksum = gps.failedChecksum();
        debug->sentences_with_fix = gps.sentencesWithFix();
        debug->location_age_ms = gps.location.age();
        debug->valid_date = gps.date.isValid();
        debug->valid_time = gps.time.isValid();
        if ( debug->valid_date ) {
            debug->year = gps.date.year();
            debug->month = gps.date.month();
            debug->day = gps.date.day();
        }
        if ( debug->valid_time ) {
            debug->hour = gps.time.hour();
            debug->minute = gps.time.minute();
            debug->second = gps.time.second();
        }
        time_t gps_epoch = gpsctl_get_gps_epoch_utc();
        debug->gps_epoch = gps_epoch > 0 ? static_cast<uint32_t>( gps_epoch ) : 0;
        debug->time_sync_count = gpsctl_time_sync_count;
        debug->last_time_sync_epoch = gpsctl_last_time_sync_epoch > 0 ? static_cast<uint32_t>( gpsctl_last_time_sync_epoch ) : 0;
        debug->last_time_sync_age_ms = gpsctl_last_time_sync_millis == 0 ? UINT32_MAX : millis() - gpsctl_last_time_sync_millis;
    }
    else {
        debug->location_age_ms = UINT32_MAX;
    }
#endif
}

#ifndef NATIVE_64BIT
static int64_t gpsctl_days_from_civil( int year, unsigned month, unsigned day ) {
    year -= month <= 2;
    const int era = ( year >= 0 ? year : year - 399 ) / 400;
    const unsigned year_of_era = static_cast<unsigned>( year - era * 400 );
    const unsigned day_of_year = ( 153 * ( month + ( month > 2 ? -3 : 9 ) ) + 2 ) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097LL + static_cast<int>( day_of_era ) - 719468LL;
}

static time_t gpsctl_get_gps_epoch_utc( void ) {
    if ( !gps.date.isValid() || !gps.time.isValid() ) {
        return( 0 );
    }

    const uint16_t year = gps.date.year();
    const uint8_t month = gps.date.month();
    const uint8_t day = gps.date.day();
    const uint8_t hour = gps.time.hour();
    const uint8_t minute = gps.time.minute();
    const uint8_t second = gps.time.second();

    if ( year < 2024 || year > 2099 ||
         month < 1 || month > 12 ||
         day < 1 || day > 31 ||
         hour > 23 || minute > 59 || second > 59 ) {
        return( 0 );
    }

    const int64_t days = gpsctl_days_from_civil( year, month, day );
    if ( days < 0 ) {
        return( 0 );
    }

    return( static_cast<time_t>( ( days * 86400LL ) + ( hour * 3600 ) + ( minute * 60 ) + second ) );
}

static void gpsctl_sync_time_from_gps( void ) {
    static uint32_t last_sync_millis = 0;
    static time_t last_sync_epoch = 0;
    const uint32_t sync_interval = 15UL * 60UL * 1000UL;

    const time_t gps_epoch = gpsctl_get_gps_epoch_utc();
    if ( gps_epoch <= 0 ) {
        return;
    }

    const uint32_t now_millis = millis();
    time_t current_epoch = 0;
    time( &current_epoch );

    const bool current_time_is_bad = current_epoch < 1704067200;
    if ( !current_time_is_bad && last_sync_millis != 0 && (uint32_t)( now_millis - last_sync_millis ) < sync_interval ) {
        return;
    }

    if ( !current_time_is_bad && llabs( static_cast<long long>( current_epoch - gps_epoch ) ) <= 2 ) {
        last_sync_millis = now_millis;
        last_sync_epoch = gps_epoch;
        return;
    }

    if ( last_sync_epoch != 0 && llabs( static_cast<long long>( last_sync_epoch - gps_epoch ) ) <= 2 &&
         (uint32_t)( now_millis - last_sync_millis ) < sync_interval ) {
        return;
    }

    if ( timesync_apply_external_time( gps_epoch ) ) {
        last_sync_millis = now_millis;
        last_sync_epoch = gps_epoch;
        gpsctl_time_sync_count++;
        gpsctl_last_time_sync_epoch = gps_epoch;
        gpsctl_last_time_sync_millis = now_millis;
        gpsctl_send_cb( GPSCTL_UPDATE_DATE, (void*)&gps_data );
        gpsctl_send_cb( GPSCTL_UPDATE_TIME, (void*)&gps_data );
        GPSCTL_INFO_LOG( "synced system time from GPS UTC epoch %ld", static_cast<long>( gps_epoch ) );
    }
}
#endif

bool gpsctl_powermgm_loop_cb( EventBits_t event, void *arg ) {
    static uint64_t nextmillis = millis();
    /*
     * check if gpsctl already init or turn off
     */
    if ( !gpsctl_init || !gpsctl_enable ) {
        return( true );
    }
    /**
     * special case
     */
    #ifdef NATIVE_64BIT

    #else
        /**
         * abort if we have no serial init
         */
        if ( gps_serial ) {
            /**
             * check for serial data and read
             */
            while ( gps_serial->available() > 0 ) {
                int c = gpsctl_read_serial_byte();
                if ( c >= 0 ) {
                    gps.encode( c );
                }
            }
            const uint32_t sentence_total = gps.passedChecksum() + gps.failedChecksum();
            if ( sentence_total != gpsctl_last_sentence_total ) {
                gpsctl_last_sentence_total = sentence_total;
                gpsctl_last_sentence_millis = millis();
            }
        }
    #endif
    /**
     * run any second
     */
    if ( nextmillis < millis() ) {
        nextmillis = millis() + GPSCTL_INTERVAL;
        #ifdef NATIVE_64BIT
        #else
            /*
            * store valid state
            */
            gps_data.valid_location = gps.location.isValid();
            gps_data.valid_speed = gps.speed.isValid();
            gps_data.valid_satellite = gps.satellites.isValid();
            gps_data.valid_altitude = gps.altitude.isValid();
            gps_data.valid_course = gps.course.isValid();
            /*
            * send FIX, UPDATE_SOURCE and UPDATE_LOCATION
            */
            if ( gps_data.valid_location != gps_data.gpsfix ) {
                gps_data.gpsfix = gps_data.valid_location;
                if ( gps_data.gpsfix ) {
                    /*
                    * send FIX and SET_APP_LOCATION event 
                    */
                    gpsctl_send_cb( GPSCTL_FIX, NULL );
                    if ( gpsctl_get_app_use_gps() ) {
                        gps_data.lat = gps.location.lat();
                        gps_data.lon = gps.location.lng();
                        gpsctl_send_cb( GPSCTL_SET_APP_LOCATION, (void*)&gps_data );
                    }
                    gpsctl_send_cb( GPSCTL_UPDATE_SOURCE, (void*)&gps_data );
                }
                else {
                    /*
                    * send NOFIX event
                    */
                    gpsctl_send_cb( GPSCTL_NOFIX, NULL );
                }
            }                
            /*
            * check for data updates
            */
            if ( gps.location.isUpdated() ) {
                gps_data.gps_source = GPS_SOURCE_GPS;
                gps_data.lat = gps.location.lat();
                gps_data.lon = gps.location.lng();
                gpsctl_send_cb( GPSCTL_UPDATE_LOCATION, (void*)&gps_data );
                GPSCTL_DEBUG_LOG("new lat/lon: %f/%f", gps_data.lat, gps_data.lon );
            }
            if ( gps.date.isUpdated() ) {
                gps_data.gps_source = GPS_SOURCE_GPS;
                gpsctl_send_cb( GPSCTL_UPDATE_DATE, (void*)&gps_data );
            }
            if ( gps.time.isUpdated() ) {
                gps_data.gps_source = GPS_SOURCE_GPS;
                gpsctl_send_cb( GPSCTL_UPDATE_TIME, (void*)&gps_data );
            }
            gpsctl_sync_time_from_gps();
            if ( gps.speed.isUpdated() ) {
                gps_data.gps_source = GPS_SOURCE_GPS;
                gps_data.speed_mph = gps.speed.mph();
                gps_data.speed_mps = gps.speed.mps();
                gps_data.speed_kmh = gps.speed.kmph();
                gpsctl_send_cb( GPSCTL_UPDATE_SPEED, (void*)&gps_data );
                GPSCTL_DEBUG_LOG("new speed: %fkmh / %fmph / %mps", gps_data.speed_kmh, gps_data.speed_mph, gps_data.speed_mps );
            }
            if ( gps.altitude.isUpdated()) {
                gps_data.gps_source = GPS_SOURCE_GPS;
                gps_data.altitude_feed = gps.altitude.feet();
                gps_data.altitude_meters = gps.altitude.meters();
                gpsctl_send_cb( GPSCTL_UPDATE_ALTITUDE, (void*)&gps_data );
                GPSCTL_DEBUG_LOG("new altitude: %fmeters / %ffeed", gps_data.altitude_meters, gps_data.altitude_feed );
            }
            if( gps.course.isUpdated() ) {
                gps_data.gps_source = GPS_SOURCE_GPS;
                gps_data.course = gps.course.value();
                gpsctl_send_cb( GPSCTL_UPDATE_COURSE, (void*)&gps_data );
                GPSCTL_DEBUG_LOG("new course: %f", gps_data.course );
            }
            if ( gps.satellites.isUpdated() ) {
                if ( gps_data.satellites != gps.satellites.value() ) {
                    gps_data.gps_source = GPS_SOURCE_GPS;
                    gps_data.satellites = gps.satellites.value();
                    gpsctl_send_cb( GPSCTL_UPDATE_SATELLITE, (void*)&gps_data );
                    GPSCTL_DEBUG_LOG("new satellites: %d", gps_data.satellites );
                }
            }
            /*
            * Update Custom GNSS values
            */
            if ( TGC_sats_in_view_gps.isUpdated() )
            {
                if ( gps_data.satellite_types.gps_satellites != atoi( TGC_sats_in_view_gps.value() ) ) {
                    gps_data.gps_source = GPS_SOURCE_GPS;
                    gps_data.satellite_types.gps_satellites = atoi( TGC_sats_in_view_gps.value() );
                    gpsctl_send_cb( GPSCTL_UPDATE_SATELLITE_TYPE, (void *)&gps_data );
                    GPSCTL_DEBUG_LOG("gps satellites: %d", gps_data.satellite_types.gps_satellites );
                }
            }
            if ( TGC_sats_in_view_glonass.isUpdated() )
            {
                if ( gps_data.satellite_types.glonass_satellites != atoi( TGC_sats_in_view_glonass.value() ) ) {
                    gps_data.gps_source = GPS_SOURCE_GPS;
                    gps_data.satellite_types.glonass_satellites = atoi( TGC_sats_in_view_glonass.value() );
                    gpsctl_send_cb( GPSCTL_UPDATE_SATELLITE_TYPE, (void *)&gps_data );
                    GPSCTL_DEBUG_LOG("glosnass satellites: %d", gps_data.satellite_types.glonass_satellites );
                }
            }
            if ( TGC_sats_in_view_baidou.isUpdated() )
            {
                if ( gps_data.satellite_types.baidou_satellites != atoi( TGC_sats_in_view_baidou.value() ) ) {
                    gps_data.gps_source = GPS_SOURCE_GPS;
                    gps_data.satellite_types.baidou_satellites = atoi( TGC_sats_in_view_baidou.value() );
                    gpsctl_send_cb( GPSCTL_UPDATE_SATELLITE_TYPE, (void *)&gps_data );
                    GPSCTL_DEBUG_LOG("baidou satellites: %d", gps_data.satellite_types.baidou_satellites );
                }
            }
        #endif // NATIVE_64BIT
    }
    return( true );
}

bool gpsctl_powermgm_event_cb( EventBits_t event, void *arg ) {
    /*
     * check if gpsctl already init
     */
    if ( !gpsctl_init ) {
        return( true );
    }

    bool retval = false;

    switch( event ) {
        case POWERMGM_STANDBY:          if ( gpsctl_config.enable_on_standby && gpsctl_enable ) {
                                            GPSCTL_INFO_LOG("standby blocked by \"enable on standby\" option");
                                        }
                                        else {
                                            GPSCTL_INFO_LOG("go standby");
                                            gpsctl_autoon_off();
                                            retval = true;
                                        }
                                        break;
        case POWERMGM_WAKEUP:           GPSCTL_INFO_LOG("go wakeup");
                                        gpsctl_autoon_on();
                                        retval = true;
                                        break;
        case POWERMGM_SILENCE_WAKEUP:   GPSCTL_INFO_LOG("go silence wakeup");
                                        gpsctl_autoon_on();
                                        retval = true;
                                        break;
    }

    return( retval );    
}

bool gpsctl_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id ) {
    /*
     * check if an callback table exist, if not allocate a callback table
     */
    if ( gpsctl_callback == NULL ) {
        gpsctl_callback = callback_init( "gpsctl" );
        if ( gpsctl_callback == NULL ) {
            GPSCTL_ERROR_LOG("gpsctl_callback alloc failed");
            while( true );
        }
    }
    /*
     * register an callback entry and return them
     */
    return( callback_register( gpsctl_callback, event, callback_func, id ) );
}

bool gpsctl_send_cb( EventBits_t event, void *arg ) {
    /*
     * call all callbacks with her event mask
     */
    return( callback_send( gpsctl_callback, event, arg ) );
}

void gpsctl_on( void ) {
    /**
     * enable gps if gps disabled
     */
    if( !gpsctl_enable ) {
        powermgm_set_lightsleep( true );
        #ifdef NATIVE_64BIT
        #else
            #if defined( M5PAPER )

            #elif defined( M5CORE2 )

            #elif defined( LILYGO_WATCH_ULTRA )
                watch.powerIoctl( WATCH_POWER_GPS, true );
            #elif defined( LILYGO_T_DECK_PLUS )
                watch.powerIoctl( WATCH_POWER_GPS, true );
            #elif defined( LILYGO_WATCH_S3 )
                watch.powerIoctl( WATCH_POWER_GPS, true );
                watch.powerIoctl( WATCH_POWER_GPS_DC_CHANNEL, true );
            #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                #if defined( LILYGO_WATCH_HAS_GPS )
                    TTGOClass *ttgo = TTGOClass::getWatch();
                    ttgo->trunOnGPS();
                #endif
            #endif
        #endif
        /**
         * force gps data update
         */
        gps_data.gpsfix = false;
        gps_data.valid_location = false;
        gps_data.valid_speed = false;
        gps_data.valid_altitude = false;
        gps_data.valid_satellite = false;
        gps_data.satellite_types.gps_satellites = 0;
        gps_data.satellite_types.glonass_satellites = 0;
        gps_data.satellite_types.baidou_satellites = 0;
        gpsctl_reset_debug_counters();
        #ifndef NATIVE_64BIT
            gpsctl_prepare_receiver_after_power_on();
        #endif
        gpsctl_config.autoon = true;
        gpsctl_config.save();
        gpsctl_enable = true;
        gpsctl_send_cb( GPSCTL_UPDATE_CONFIG, NULL );
        gpsctl_send_cb( GPSCTL_ENABLE, NULL );
        gpsctl_send_cb( GPSCTL_NOFIX, NULL );
    }
#ifndef NATIVE_64BIT
    else if ( gps_serial && ( gpsctl_last_rx_millis == 0 || (uint32_t)( millis() - gpsctl_last_rx_millis ) > 5000 ) ) {
        gpsctl_reset_debug_counters();
        gpsctl_prepare_receiver_after_power_on();
    }
#endif
}

void gpsctl_off( void ) {
    /**
     * disable gps if gps enabled
     */
    if( gpsctl_enable ) {
        powermgm_set_lightsleep( true );
        #ifdef NATIVE_64BIT
        #else
            #if defined( M5PAPER )

            #elif defined( M5CORE2 )

            #elif defined( LILYGO_WATCH_ULTRA )
                watch.powerIoctl( WATCH_POWER_GPS, false );
            #elif defined( LILYGO_T_DECK_PLUS )
                watch.powerIoctl( WATCH_POWER_GPS, false );
            #elif defined( LILYGO_WATCH_S3 )
                watch.powerIoctl( WATCH_POWER_GPS, false );
                watch.powerIoctl( WATCH_POWER_GPS_DC_CHANNEL, false );
            #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                #if defined( LILYGO_WATCH_HAS_GPS )
                    TTGOClass *ttgo = TTGOClass::getWatch();
                    ttgo->turnOffGPS();
                #endif
            #endif
        #endif
        /**
         * force gps data update
         */
        gps_data.gpsfix = false;
        gps_data.valid_location = false;
        gps_data.valid_speed = false;
        gps_data.valid_altitude = false;
        gps_data.valid_satellite = false;
        gps_data.satellite_types.gps_satellites = 0;
        gps_data.satellite_types.glonass_satellites = 0;
        gps_data.satellite_types.baidou_satellites = 0;
        gpsctl_config.autoon = false;
        gpsctl_config.save();
        gpsctl_enable = false;
        gpsctl_send_cb( GPSCTL_UPDATE_CONFIG, NULL );
        gpsctl_send_cb( GPSCTL_NOFIX, NULL );
        gpsctl_send_cb( GPSCTL_DISABLE, NULL );
    }
}

void gpsctl_autoon_on( void ) {
    gps_data.gpsfix = false;
    gps_data.valid_location = false;
    gps_data.valid_speed = false;
    gps_data.valid_altitude = false;
    gps_data.valid_satellite = false;
    gps_data.satellite_types.gps_satellites = 0;
    gps_data.satellite_types.glonass_satellites = 0;
    gps_data.satellite_types.baidou_satellites = 0;

    if ( gpsctl_config.autoon ) {
        if ( !gpsctl_enable ) {
            gpsctl_reset_debug_counters();
            #ifdef NATIVE_64BIT
            #else
                #if defined( M5PAPER )

                #elif defined( M5CORE2 )

                #elif defined( LILYGO_WATCH_ULTRA )
                    watch.powerIoctl( WATCH_POWER_GPS, true );
                #elif defined( LILYGO_T_DECK_PLUS )
                    watch.powerIoctl( WATCH_POWER_GPS, true );
                #elif defined( LILYGO_WATCH_S3 )
                    watch.powerIoctl( WATCH_POWER_GPS, true );
                    watch.powerIoctl( WATCH_POWER_GPS_DC_CHANNEL, true );
                #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                    #if defined( LILYGO_WATCH_HAS_GPS )
                        TTGOClass *ttgo = TTGOClass::getWatch();
                        ttgo->trunOnGPS();
                    #endif
                #endif
            #endif
            gpsctl_enable = true;
            #ifndef NATIVE_64BIT
                gpsctl_prepare_receiver_after_power_on();
            #endif
            gpsctl_send_cb( GPSCTL_ENABLE, NULL );
            gpsctl_send_cb( GPSCTL_NOFIX, NULL );
            powermgm_set_lightsleep( true );
        }
    }
    else {
        #ifdef NATIVE_64BIT
        #else
            #if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )
                watch.powerIoctl( WATCH_POWER_GPS, false );
            #endif
        #endif
        gpsctl_enable = false;
        gpsctl_send_cb( GPSCTL_NOFIX, NULL );
        gpsctl_send_cb( GPSCTL_DISABLE, NULL );
        powermgm_set_lightsleep( true );
    }
}

void gpsctl_autoon_off( void ) {
    #ifdef NATIVE_64BIT
    #else
        #if defined( M5PAPER )

        #elif defined( M5CORE2 )

        #elif defined( LILYGO_WATCH_ULTRA )
            watch.powerIoctl( WATCH_POWER_GPS, false );
        #elif defined( LILYGO_T_DECK_PLUS )
            watch.powerIoctl( WATCH_POWER_GPS, false );
        #elif defined( LILYGO_WATCH_S3 )
            watch.powerIoctl( WATCH_POWER_GPS, false );
            watch.powerIoctl( WATCH_POWER_GPS_DC_CHANNEL, false );
        #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            #if defined( LILYGO_WATCH_HAS_GPS )
                TTGOClass *ttgo = TTGOClass::getWatch();
                ttgo->turnOffGPS();
            #endif
        #endif
    #endif
    gpsctl_enable = false;
    gps_data.gpsfix = false;
    gps_data.valid_location = false;
    gps_data.valid_speed = false;
    gps_data.valid_altitude = false;
    gps_data.valid_satellite = false;
    gps_data.satellite_types.gps_satellites = 0;
    gps_data.satellite_types.glonass_satellites = 0;
    gps_data.satellite_types.baidou_satellites = 0;
    gpsctl_send_cb( GPSCTL_NOFIX, NULL );
    gpsctl_send_cb( GPSCTL_DISABLE, NULL );
    powermgm_set_lightsleep( true );
}

bool gpsctl_get_app_use_gps( void ) {
    return( gpsctl_config.app_use_gps );
}

void gpsctl_set_app_use_gps( bool app_use_gps ) {
    gpsctl_config.app_use_gps = app_use_gps;
    gpsctl_config.save();
    gpsctl_send_cb( GPSCTL_UPDATE_CONFIG, NULL );
}

bool gpsctl_get_autoon( void ) {
    return( gpsctl_config.autoon );
}

void gpsctl_set_autoon( bool autoon ) {
    gpsctl_config.autoon = autoon;
    gpsctl_config.save();
    gpsctl_send_cb( GPSCTL_UPDATE_CONFIG, NULL );
}

bool gpsctl_get_gps_over_ip( void ) {
    return( gpsctl_config.gps_over_ip );
}

void gpsctl_set_gps_rx_tx_pin( int8_t rx, int8_t tx ) {
    #if defined( LILYGO_WATCH_ULTRA ) || defined( LILYGO_T_DECK_PLUS )
        rx = SHIELD_GPS_RX;
        tx = SHIELD_GPS_TX;
    #endif
    gpsctl_config.RXPin = rx;
    gpsctl_config.TXPin = tx;
    gpsctl_config.save();
    GPSCTL_DEBUG_LOG("set new rx/tx pin %d/%d", rx, tx );
}

void gpsctl_get_gps_rx_tx_pin( int8_t *rx, int8_t *tx ) {
    *rx = gpsctl_config.RXPin;
    *tx = gpsctl_config.TXPin;
}

void gpsctl_set_gps_over_ip( bool gps_over_ip ) {
    gpsctl_config.gps_over_ip = gps_over_ip;
    gpsctl_config.save();
    gpsctl_send_cb( GPSCTL_UPDATE_CONFIG, NULL );
}

bool gpsctl_get_enable_on_standby( void ) {
    return( gpsctl_config.enable_on_standby );
}

void gpsctl_set_enable_on_standby( bool enable_on_standby ) {
    gpsctl_config.enable_on_standby = enable_on_standby;
    gpsctl_config.save();
    gpsctl_send_cb( GPSCTL_UPDATE_CONFIG, NULL );
}

void gpsctl_set_location( double lat, double lon, double altitude, double speed, gps_source_t gps_source , bool app_location ) {
    /*
     * setup gps_data structure and send events
     */
    if ( !gps_data.gpsfix ) {
        gps_data.gpsfix = true;
        gpsctl_send_cb( GPSCTL_FIX, NULL );
    }
    if ( gps_data.gps_source != gps_source ) {
        gps_data.gps_source = gps_source;
        gpsctl_send_cb( GPSCTL_UPDATE_SOURCE, (void*)&gps_data );        
    }
    gps_data.valid_location = true;
    gps_data.valid_speed = true;
    gps_data.valid_satellite = false;
    gps_data.valid_altitude = true;
    gps_data.lat = lat;
    gps_data.lon = lon;
    gps_data.altitude_meters = altitude;
    gps_data.speed_kmh = speed;
    /*
     * send FIX, UPDATE_SOURCE and UPDATE_LOCATION
     */
    gpsctl_send_cb( GPSCTL_UPDATE_LOCATION, (void*)&gps_data );
    gpsctl_send_cb( GPSCTL_UPDATE_ALTITUDE, (void*)&gps_data );
    /*
     * send SET_APP_LOCATION if enabled
     */
    if ( gpsctl_get_app_use_gps() && app_location ) {
        gpsctl_send_cb( GPSCTL_SET_APP_LOCATION, (void*)&gps_data );
    }
}

const char *gpsctl_get_source_str( gps_source_t gps_source ) {
    const char *ret_val = NULL;

    switch( gps_source ) {
        case GPS_SOURCE_UNKNOWN:
            ret_val = "unknown gps";
            break;
        case GPS_SOURCE_FAKE:
            ret_val = "fake gps";
            break;
        case GPS_SOURCE_IP:
            ret_val = "ip location";
            break;
        case GPS_SOURCE_USER:
            ret_val = "user gps";
            break;
        case GPS_SOURCE_GPS:
            ret_val = "gps receiver";
            break;
        default:
            ret_val = "no source";
    }

    return( ret_val );
}

double gpsctl_distance( double lat1, double long1, double lat2, double long2, double earth_radius ) {
    double dlong = ( long2 - long1 ) * M_PI / 180.0;
    double dlat = ( lat2 - lat1 ) * M_PI / 180.0;
    double a = pow( sin( dlat / 2.0 ), 2 ) + cos( lat1 * M_PI / 180.0 ) * cos( lat2 * M_PI / 180.0 ) * pow( sin( dlong / 2.0 ), 2 );
    double c = 2 * atan2( sqrt( a ), sqrt( 1 - a ) );
    double d = earth_radius * c;

    return( d );
}


double gpsctl_courseTo( double lat1, double long1, double lat2, double long2 ) {
    double dlong = radians(long2-long1);
    lat1 = radians(lat1);
    lat2 = radians(lat2);
    double a1 = sin(dlong) * cos(lat2);
    double a2 = sin(lat1) * cos(lat2) * cos(dlong);
    a2 = cos(lat1) * sin(lat2) - a2;
    a2 = atan2(a1, a2);
    
    if (a2 < 0.0) {
        a2 += ( M_PI * 2 );
    }
    
    return degrees(a2);
}
