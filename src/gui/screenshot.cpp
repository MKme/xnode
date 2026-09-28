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
#include <endian.h>
#include "screenshot.h"
#include "utils/alloc.h"
#include "utils/filepath_convert.h"
#include "gui/png_decoder/lv_png.h"
#include "hardware/motor.h"
#include "gui/gui.h"

#ifdef NATIVE_64BIT
    #include <iostream>
    #include <fstream>
    #include <string.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

static raw_img_grey_t *raw_grey;
static raw_img_rgb_t *raw_rgb;

static void screenshot_disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p );

#if (defined(LILYGO_T_DECK_PLUS) || defined(LILYGO_WATCH_ULTRA)) && !defined(NATIVE_64BIT)
#include "app/meshtastic/meshtastic_service.h"
void screenshot_usb_poll( void ) {
    // No network endpoint, navigation, storage writes or radio actions. An exact
    // newline-terminated request is required; overflow discards the whole line.
    static char command[32];
    static size_t used = 0;
    static bool overflow = false;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') {
            if (used < sizeof(command) - 1 && !overflow) command[used++] = c;
            else overflow = true;
            continue;
        }
        command[used] = 0;
        const bool capture = !overflow && strcmp(command, "XNODE SCREENSHOT") == 0;
        const bool status = !overflow && strcmp(command, "XNODE STATUS") == 0;
        used = 0;
        overflow = false;
        if (status) {
            // Read-only diagnostics: no keys, message bodies or configuration writes.
            Serial.printf("\nXNODE_STATUS ready=%u receiving=%u frequency=%.3f history_revision=%lu heap=%u stack_min=%u status=%s\n",
                meshtastic_service_is_ready(), meshtastic_service_is_receiving(),
                meshtastic_service_get_frequency_mhz(),
                (unsigned long)meshtastic_service_get_history_revision(),
                (unsigned)ESP.getFreeHeap(), (unsigned)uxTaskGetStackHighWaterMark(NULL),
                meshtastic_service_get_status());
            continue;
        }
        if (!capture) continue;
        if (!gui_take()) return;
        screenshot_take();
        const int width = lv_disp_get_hor_res(NULL);
        const int height = lv_disp_get_ver_res(NULL);
        if (raw_rgb && width > 0 && width <= 362 && height > 0 && height <= 440) {
            Serial.printf("\nXNODE_SCREEN_BEGIN %d %d RGB888\n", width, height);
            static const char hex[] = "0123456789abcdef";
            char row[362 * 6 + 1];
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const rgb_t &pixel = raw_rgb->data[y * width + x];
                    const uint8_t channels[] = {pixel.r, pixel.g, pixel.b};
                    for (int c = 0; c < 3; ++c) {
                        row[x * 6 + c * 2] = hex[channels[c] >> 4];
                        row[x * 6 + c * 2 + 1] = hex[channels[c] & 15];
                    }
                }
                row[width * 6] = 0;
                Serial.printf("XNODE_SCREEN_ROW %d %s\n", y, row);
                delay(1);
            }
            Serial.println("XNODE_SCREEN_END");
        } else {
            Serial.println("XNODE_SCREEN_ERROR capture unavailable");
        }
        free(raw_rgb);
        raw_rgb = NULL;
        gui_give();
        return;
    }
}
#endif

void screenshot_setup( void ) {
    raw_grey = NULL;
    raw_rgb = NULL;
}

void screenshot_take( void ) {
    lv_disp_drv_t driver;
    lv_disp_t *system_disp;
    /**
     * force reflush lvgl image cache
     */
    lv_img_cache_set_size( 1 );
    lv_img_cache_set_size( 256 );
    /**
     * allocate screenshot memory
     */
    #if defined( MONOCHROME ) || defined( MONOCHROME_4BIT ) || defined( MONOCHROME_EINK )
        if ( !raw_grey ) {
            raw_grey = (raw_img_grey_t*)MALLOC( sizeof( raw_img_grey_t ) );
            if ( raw_grey == NULL ) {
                log_e("screenshot malloc failed");
                return;
            }
        }
    #else
        if ( !raw_rgb ) {
            raw_rgb = (raw_img_rgb_t*)MALLOC( sizeof( raw_img_rgb_t ) );
            if ( raw_rgb == NULL ) {
                log_e("screenshot malloc failed");
                return;
            }
        }
    #endif

    log_i("take screenshot");
    /**
     * redirect display driver
     */
    system_disp = lv_disp_get_default();
    driver.flush_cb = system_disp->driver.flush_cb;
    system_disp->driver.flush_cb = screenshot_disp_flush;
    lv_obj_invalidate( lv_scr_act() );
    lv_refr_now( system_disp );
    system_disp->driver.flush_cb = driver.flush_cb;
}

void screenshot_save( void ) {
    /**
     * check if screenshot memory allocated
     */
    if ( raw_grey == NULL && raw_rgb == NULL ) {
        log_e("no screenshot memory allocated");
        return;
    }
    /**
     * genrate local filename + path
     */
    char filename[256] = "";
    filepath_convert( filename, sizeof( filename ), SCREENSHOT_FILE_NAME );
    /**
     * delete old screenshot
     */
    remove( filename );
    /**
     * open new screenshoot file and write them
     */
    if ( raw_grey ) {
        log_i("save 8bit grey screenshot");
        /**
         * save img buffer as png
         */
        lv_8grey_as_png( filename, (const uint8_t*)raw_grey, RES_X_MAX, RES_Y_MAX );
        /**
         * free screenshot memory
         */
        free( raw_grey );
        raw_grey = NULL;
    }
    if ( raw_rgb ) {
        log_i("save rgba screenshot");
        /**
         * save img buffer as png
         */
        lv_rgb_as_png( filename, (const uint8_t*)raw_rgb, RES_X_MAX, RES_Y_MAX );
        /**
         * free screenshot memory
         */
        free( raw_rgb );
        raw_rgb = NULL;
    }

    motor_vibe( 10, true );
}

static void screenshot_disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p ) {

    uint32_t x, y;
    lv_color_t *color = color_p;
    /**
     * check if screenshot memory allocated
     */
    if ( raw_rgb == NULL && raw_grey == NULL ) {
        log_e("no screenshot memory allocated");
        return;
    }
    /**
     * copy data into screenshot memory as RGBA
     */
    for(y = area->y1; y <= area->y2; y++) {
        for(x = area->x1; x <= area->x2; x++) {
            uint8_t r,g,b;
            switch( LV_COLOR_DEPTH ) {
                case 8:     r = LV_COLOR_GET_R( *color ) << 5;
                            g = LV_COLOR_GET_G( *color ) << 5;
                            b = LV_COLOR_GET_B( *color ) << 6;
                            break;
                case 16:    r = LV_COLOR_GET_R( *color ) << 3;
                            g = LV_COLOR_GET_G( *color ) << 2;
                            b = LV_COLOR_GET_B( *color ) << 3;
                            break;
                case 32:    r = LV_COLOR_GET_R( *color );
                            g = LV_COLOR_GET_G( *color );
                            b = LV_COLOR_GET_B( *color );
                            break;
                default:    r = g = b = 0;
                            break;
            }
            if ( raw_grey ) {
                raw_grey->data[ ( y * lv_disp_get_hor_res( NULL ) ) + x ].grey = lv_color_brightness( *color );
            }
            if ( raw_rgb ) {
                raw_rgb->data[ y * lv_disp_get_hor_res( NULL ) + x ].r = r;
                raw_rgb->data[ y * lv_disp_get_hor_res( NULL ) + x ].g = g;
                raw_rgb->data[ y * lv_disp_get_hor_res( NULL ) + x ].b = b;
            }
            color++;
        }
    }
    lv_disp_flush_ready(disp_drv);
}
