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
#include "displayconfig.h"

static uint32_t display_default_brightness( void ) {
#if defined( LILYGO_WATCH_ULTRA )
    return DISPLAY_MAX_BRIGHTNESS / 2;
#else
    return DISPLAY_MAX_BRIGHTNESS / 2;
#endif
}

display_config_t::display_config_t() : BaseJsonConfig(DISPLAY_JSON_CONFIG_FILE) {
}

bool display_config_t::onSave(JsonDocument& doc) {
    doc["config_version"] = DISPLAY_CONFIG_VERSION;
    doc["brightness"] = brightness;
    doc["rotation"] = rotation;
    doc["timeout"] = timeout;
    doc["block_return_maintile"] = block_return_maintile;
    doc["background_image"] = background_image;
    doc["use_dma"] = use_dma;
    doc["use_double_buffering"] = use_double_buffering;
    doc["vibe"] = vibe;

    return true;
}

bool display_config_t::onLoad(JsonDocument& doc) {
    const uint32_t loaded_config_version = doc["config_version"] | 0;
    config_version = loaded_config_version;
    migrated_legacy_timeout = false;
    migrated_power_profile = false;
    brightness = doc["brightness"] | display_default_brightness();
    rotation = doc["rotation"] | DISPLAY_MIN_ROTATE;
    timeout = doc["timeout"] | DISPLAY_MIN_TIMEOUT;

    if ( loaded_config_version < DISPLAY_CONFIG_VERSION && timeout == DISPLAY_MAX_TIMEOUT ) {
        timeout = DISPLAY_MIN_TIMEOUT;
        migrated_legacy_timeout = true;
    }

#if defined( LILYGO_WATCH_ULTRA )
    if ( loaded_config_version < DISPLAY_CONFIG_VERSION && brightness > display_default_brightness() ) {
        brightness = display_default_brightness();
        migrated_power_profile = true;
    }
#endif

    if ( timeout < DISPLAY_MIN_TIMEOUT ) {
        timeout = DISPLAY_MIN_TIMEOUT;
    }
    else if ( timeout > DISPLAY_MAX_TIMEOUT ) {
        timeout = DISPLAY_MAX_TIMEOUT;
    }

    config_version = DISPLAY_CONFIG_VERSION;
    block_return_maintile = doc["block_return_maintile"] | false;
    background_image = doc["background_image"] | 4;
    use_dma = doc["use_dma"] | true;
    use_double_buffering = doc["use_double_buffering"] | false;
    vibe = doc["vibe"] | true;

    return true;
}

bool display_config_t::onDefault( void ) {
    config_version = DISPLAY_CONFIG_VERSION;
    brightness = display_default_brightness();
    timeout = DISPLAY_MIN_TIMEOUT;
    rotation = DISPLAY_MIN_ROTATE;
    block_return_maintile = false;
    background_image = 4;
    use_dma = true;
    use_double_buffering = false;
    vibe = true;
    migrated_legacy_timeout = false;
    migrated_power_profile = false;
    return true;
}
