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
#include "lvgl.h"
#include "display.h"
#include "powermgm.h"
#include "framebuffer.h"
#include "motion.h"

#ifdef NATIVE_64BIT
    #include "utils/millis.h"
    #include "utils/logging.h"
#else
    #if defined( M5PAPER )
        #include <M5EPD.h>
    #elif defined( M5CORE2 )
        #include <M5Core2.h>
    #elif defined( LILYGO_WATCH_S3 )
        #include <LilyGoLib.h>
    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        #include <TTGO.h>
    #elif defined( LILYGO_WATCH_2021 )
        #include <twatch2021_config.h>
    #elif defined( WT32_SC01 )
    #else
        #error "no hardware driver for display, please setup minimal drivers ( display/framebuffer/touch )"
    #endif
#endif

display_config_t display_config;
callback_t *display_callback = NULL;

static uint8_t dest_brightness = 0;
static uint8_t brightness = 0;
static uint32_t display_timeout = DISPLAY_MIN_TIMEOUT;
static uint32_t display_last_activity_ms = 0;

static bool display_powermgm_event_cb( EventBits_t event, void *arg );
static bool display_powermgm_loop_cb( EventBits_t event, void *arg );
static bool display_send_event_cb( EventBits_t event, void *arg );
static uint32_t display_sanitize_timeout( uint32_t timeout, bool allow_no_timeout );
static void display_update_timeout_dimmer( void );
static void display_standby( void );
static void display_wakeup( bool silence );

void display_setup( void ) {
    /**
     * load config from json
     */
    display_config.load();
    display_timeout = display_sanitize_timeout( display_config.timeout, false );
    display_config.timeout = display_timeout;
    if ( display_config.migrated_legacy_timeout ) {
        log_i( "migrating legacy display timeout 300->%u seconds", display_timeout );
        display_config.save();
        display_config.migrated_legacy_timeout = false;
    }
    display_note_activity();
    log_i( "display config: brightness=%u timeout=%u", display_config.brightness, display_timeout );
    /**
     * setup backlight and rotation
     */
    #ifdef NATIVE_64BIT
    #else
        #if defined( M5PAPER )

        #elif defined( M5CORE2 )
            M5.Axp.SetLcdVoltage( 2532 + display_get_brightness() );
        #elif defined( LILYGO_WATCH_S3 )
            watch.setRotation( display_config.rotation / 90 );
            watch.setBrightness( 0 );
            bma_set_rotate_tilt( display_config.rotation );
        #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            TTGOClass *ttgo = TTGOClass::getWatch();
            ttgo->tft->init();
            ttgo->openBL();
            ttgo->bl->adjust( 0 );
            ttgo->tft->setRotation( display_config.rotation / 90 );
            bma_set_rotate_tilt( display_config.rotation );
        #elif defined( LILYGO_WATCH_2021 )
            pinMode(TFT_LED, OUTPUT);
            ledcSetup(0, 4000, 8);
            ledcAttachPin(TFT_LED, 0);
            ledcWrite(0, 0XFF);
        #elif defined( WT32_SC01 )
            pinMode(TFT_LED, OUTPUT);
            ledcSetup(0, 4000, 8);
            ledcAttachPin(TFT_LED, 0);
            ledcWrite(0, 0x0 );
        #else
            #error "no display init function implemented, please setup minimal drivers ( display/framebuffer/touch )"
        #endif
    #endif
    /**
     * setup framebuffer
     */
    framebuffer_setup();
    /**
     * register powermgm and pwermgm loop callback functions
     */
    powermgm_register_cb_with_prio( POWERMGM_STANDBY, display_powermgm_event_cb, "powermgm display", CALL_CB_FIRST );
    powermgm_register_cb_with_prio( POWERMGM_SILENCE_WAKEUP | POWERMGM_WAKEUP, display_powermgm_event_cb, "powermgm display", CALL_CB_LAST );
    powermgm_register_loop_cb( POWERMGM_WAKEUP, display_powermgm_loop_cb, "powermgm display loop" );
}

static bool display_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:          display_standby();
                                        break;
        case POWERMGM_WAKEUP:           display_wakeup( false );
                                        break;
        case POWERMGM_SILENCE_WAKEUP:   display_wakeup( true );
                                        break;
    }
    return( true );
}

static bool display_powermgm_loop_cb( EventBits_t event, void *arg ) {
    bool retval = false;

    #ifdef NATIVE_64BIT
    #else
        #if defined( M5PAPER )

        #elif defined( M5CORE2 )
            if ( dest_brightness != brightness ) {
                if ( brightness < dest_brightness ) {
                    brightness++;
                    M5.Axp.SetLcdVoltage( 2532 + brightness );
                }
                else {
                    brightness--;
                    M5.Axp.SetLcdVoltage( 2532 + brightness );
                }
            }
            display_update_timeout_dimmer();

            retval = true;
        #elif defined( LILYGO_WATCH_S3 )
            if ( dest_brightness != brightness ) {
                if ( brightness < dest_brightness ) {
                    brightness++;
                }
                else {
                    brightness--;
                }
                watch.setBrightness( brightness );
            }
            display_update_timeout_dimmer();

            retval = true;
        #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            TTGOClass *ttgo = TTGOClass::getWatch();
            /**
             * check if backlight adjust has change
             */
            if ( dest_brightness != brightness ) {
                if ( brightness < dest_brightness ) {
                    brightness++;
                    ttgo->bl->adjust( brightness );
                }
                else {
                    brightness--;
                    ttgo->bl->adjust( brightness );
                }
            }
            display_update_timeout_dimmer();

            retval = true;
        #elif defined( LILYGO_WATCH_2021 )   
            /**
             * check if backlight adjust has change
             */
            if ( dest_brightness != brightness ) {
                if ( brightness < dest_brightness ) {
                    brightness++;
                    ledcWrite(0, brightness );
                }
                else {
                    brightness--;
                    ledcWrite(0, brightness );
                }
            }
            display_update_timeout_dimmer();

            retval = true;
        #elif defined( WT32_SC01 )
            /**
             * check if backlight adjust has change
             */
            if ( dest_brightness != brightness ) {
                if ( brightness < dest_brightness ) {
                    brightness++;
                    ledcWrite(0, brightness );
                }
                else {
                    brightness--;
                    ledcWrite(0, brightness );
                }
            }
            display_update_timeout_dimmer();

            retval = true;
        #else
            #error "no display init function implemented, please setup minimal drivers ( display/framebuffer/touch )"
        #endif
    #endif

    return( retval );
}

bool display_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id ) {
    if ( display_callback == NULL ) {
        display_callback = callback_init( "display" );
        if ( display_callback == NULL ) {
            log_e("display_callback_callback alloc failed");
            while(true);
        }
    }
    return( callback_register( display_callback, event, callback_func, id ) );
}

static bool display_send_event_cb( EventBits_t event, void *arg ) {
    return( callback_send( display_callback, event, arg ) );
}

static uint32_t display_sanitize_timeout( uint32_t timeout, bool allow_no_timeout ) {
    if ( allow_no_timeout && timeout == DISPLAY_NO_TIMEOUT ) {
        return( DISPLAY_NO_TIMEOUT );
    }
    if ( timeout < DISPLAY_MIN_TIMEOUT ) {
        return( DISPLAY_MIN_TIMEOUT );
    }
    if ( timeout > DISPLAY_MAX_TIMEOUT ) {
        return( DISPLAY_MAX_TIMEOUT );
    }
    return( timeout );
}

static void display_update_timeout_dimmer( void ) {
    const uint32_t configured_brightness = display_get_brightness();
    const uint32_t timeout = display_get_timeout();

    if ( timeout == DISPLAY_NO_TIMEOUT ) {
        dest_brightness = configured_brightness;
        return;
    }

    const uint32_t timeout_ms = timeout * 1000;
    const uint32_t inactive_ms = display_get_inactive_time_ms();
    const uint32_t fade_window_ms = configured_brightness * 8;
    const uint32_t fade_start_ms = timeout_ms > fade_window_ms ? timeout_ms - fade_window_ms : 0;

    if ( inactive_ms >= timeout_ms ) {
        dest_brightness = 0;
    }
    else if ( inactive_ms > fade_start_ms ) {
        dest_brightness = ( timeout_ms - inactive_ms ) / 8;
    }
    else {
        dest_brightness = configured_brightness;
    }
}

static void display_standby( void ) {
    #ifdef NATIVE_64BIT
    #else
        #if defined( M5PAPER )

        #elif defined( M5CORE2 )
            M5.Lcd.sleep();
        #elif defined( LILYGO_WATCH_S3 )
            watch.setBrightness( 0 );
            brightness = 0;
            dest_brightness = 0;
        #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            TTGOClass *ttgo = TTGOClass::getWatch();
            ttgo->bl->adjust( 0 );
            ttgo->displaySleep();
            ttgo->closeBL();
            brightness = 0;
            dest_brightness = 0;
            #if defined( LILYGO_WATCH_2020_V2 )
                ttgo->power->setLDO2Voltage( 3300 );
                ttgo->power->setLDO3Voltage( 3300 );
                ttgo->power->setPowerOutPut( AXP202_LDO2, false );
                ttgo->power->setPowerOutPut( AXP202_LDO3, false );
            #endif
        #elif defined( LILYGO_WATCH_2021 )   
            ledcWrite( 0, 0 );
        #elif defined( WT32_SC01 )
            ledcWrite( 0, 0 );
        #else
            #error "no display statndby function implemented, please setup minimal drivers ( display/framebuffer/touch )"
        #endif
    #endif
    log_d("go standby");
}

static void display_wakeup( bool silence ) {
    display_note_activity();
    /**
     * wakeup with or without display
     */
    if ( silence ) {
        #ifdef NATIVE_64BIT
        #else
            #if defined( M5PAPER )
                M5.enableEPDPower();
                delay(25);
            #elif defined( M5CORE2 )
                M5.Axp.SetLcdVoltage( 2532 + display_get_brightness() );
                brightness = 0;
                dest_brightness = 0;
            #elif defined( LILYGO_WATCH_S3 )
                watch.setBrightness( 0 );
                brightness = 0;
                dest_brightness = 0;
            #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                TTGOClass *ttgo = TTGOClass::getWatch();
                #if defined( LILYGO_WATCH_2020_V2 )
                    ttgo->power->setLDO2Voltage( 3300 );
                    ttgo->power->setLDO3Voltage( 3300 );
                    ttgo->power->setPowerOutPut( AXP202_LDO2, true );
                    ttgo->power->setPowerOutPut( AXP202_LDO3, true );
                #endif
                ttgo->openBL();
                ttgo->displayWakeup();
                ttgo->bl->adjust( 0 );
                brightness = 0;
                dest_brightness = 0;
            #elif defined( LILYGO_WATCH_2021 )   
                ledcWrite( 0, 0 );
                brightness = 0;
                dest_brightness = 0;
            #elif defined( WT32_SC01 )
                ledcWrite( 0, 0 );
                brightness = 0;
                dest_brightness = 0;
            #else
                #error "no silence display wakeup function implemented, please setup minimal drivers ( display/framebuffer/touch )"
            #endif
        #endif
        log_d("go silence wakeup");
    }
    else {
        #ifdef NATIVE_64BIT
        #else
            #if defined( M5PAPER )
                M5.enableEPDPower();
                delay(25);
            #elif defined( M5CORE2 )
                M5.Lcd.begin();
                M5.Lcd.wakeup();
                M5.Axp.SetLcdVoltage( 2532 + display_get_brightness() );
            #elif defined( LILYGO_WATCH_S3 )
                brightness = 0;
                dest_brightness = display_get_brightness();
            #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                TTGOClass *ttgo = TTGOClass::getWatch();
                #if defined( LILYGO_WATCH_2020_V2 )
                    ttgo->power->setLDO2Voltage( 3300 );
                    ttgo->power->setLDO3Voltage( 3300 );
                    ttgo->power->setPowerOutPut( AXP202_LDO2, true );
                    ttgo->power->setPowerOutPut( AXP202_LDO3, true );
                #endif
                ttgo->openBL();
                ttgo->displayWakeup();
                ttgo->bl->adjust( 0 );
                brightness = 0;
                dest_brightness = display_get_brightness();
            #elif defined( LILYGO_WATCH_2021 )   
                ledcWrite( 0, 0 );
                brightness = 0;
                dest_brightness = display_get_brightness();
            #elif defined( WT32_SC01 )
                ledcWrite( 0, 0 );
                brightness = 0;
                dest_brightness = display_get_brightness();
            #else
                #error "no display wakeup function implemented, please setup minimal drivers ( display/framebuffer/touch )"
            #endif
        #endif
        log_d("go wakeup");
    }
}

void display_save_config( void ) {
      display_config.timeout = display_sanitize_timeout( display_config.timeout, false );
      display_config.save();
}

void display_read_config( void ) {
    display_config.load();
    display_timeout = display_sanitize_timeout( display_config.timeout, false );
    display_config.timeout = display_timeout;
}

void display_note_activity( void ) {
    display_last_activity_ms = millis();
}

void display_trigger_activity( void ) {
    display_note_activity();
    lv_disp_trig_activity( NULL );
}

uint32_t display_get_inactive_time_ms( void ) {
    return( millis() - display_last_activity_ms );
}

uint32_t display_get_timeout( void ) {
    return( display_timeout );
}

void display_set_timeout( uint32_t timeout ) {
    display_timeout = display_sanitize_timeout( timeout, true );
    if ( display_timeout != DISPLAY_NO_TIMEOUT ) {
        display_config.timeout = display_timeout;
    }
    display_send_event_cb( DISPLAYCTL_TIMEOUT, (void *)&display_timeout );
}

uint32_t display_get_brightness( void ) {
    return( display_config.brightness );
}

void display_set_brightness( uint32_t brightness_level ) {
    display_config.brightness = brightness_level;
    dest_brightness = brightness_level;
    #ifdef NATIVE_64BIT

    #else
        #if defined ( M5CORE2 )
        M5.Axp.SetLcdVoltage( 2532 + display_get_brightness() );
        #elif defined( LILYGO_WATCH_S3 )
        brightness = brightness_level;
        watch.setBrightness( brightness_level );
        #endif
    #endif
    display_send_event_cb( DISPLAYCTL_BRIGHTNESS, (void *)&display_config.brightness );
}

uint32_t display_get_rotation( void ) {
    return( display_config.rotation );
}

bool display_get_block_return_maintile( void ) {
    return( display_config.block_return_maintile );
}

bool display_get_use_double_buffering( void ) {
    return( display_config.use_double_buffering );
}

void display_set_use_double_buffering( bool use_double_buffering ) {
    display_config.use_double_buffering = use_double_buffering;
}

bool display_get_use_dma( void ) {
    return( display_config.use_dma );
}

void display_set_use_dma( bool use_dma ) {
    display_config.use_dma = use_dma;
}

void display_set_block_return_maintile( bool block_return_maintile ) {
    display_config.block_return_maintile = block_return_maintile;
}

void display_set_rotation( uint32_t rotation ) {
    #ifdef NATIVE_64BIT
    #else
        #if defined( M5PAPER )
        #elif defined( M5CORE2 )
        #elif defined( LILYGO_WATCH_S3 )
            display_config.rotation = rotation;
            watch.setRotation( rotation / 90 );
        #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            TTGOClass *ttgo = TTGOClass::getWatch();
            display_config.rotation = rotation;
            ttgo->tft->setRotation( rotation / 90 );
        #elif defined( WT32_SC01 )
        #else
            #warning "no display set rotation function implemented, please setup minimal drivers ( display/framebuffer/touch )"
        #endif
    #endif
    display_config.rotation = rotation;
}

uint32_t display_get_background_image( void ) {
    return( display_config.background_image );
}

void display_set_background_image( uint32_t background_image ) {
    display_config.background_image = background_image;
}

void display_set_vibe( bool vibe ) {
    display_config.vibe = vibe;
}

bool display_get_vibe( void ) {
    return display_config.vibe;
}
