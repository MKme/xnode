/****************************************************************************
 *   Aug 11 17:13:51 2020
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
#include "styleconfig.h"
#include "utils/alloc.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else

#endif

style_config_t::style_config_t() : BaseJsonConfig( STYLE_JSON_COFIG_FILE ) {}

bool style_config_t::onSave(JsonDocument& doc) {
    doc["theme"] = theme;
    doc["anim"] = anim;
    doc["theme_migrated"] = true;
    needs_save = false;
    return true;
}

bool style_config_t::onLoad(JsonDocument& doc) {
    #if defined( M5PAPER )
        theme = doc["theme"] | 0;
        anim = doc["anim"] | false;
    #elif defined( LILYGO_T_DECK_PLUS )
        theme = doc["theme"] | 1;
        anim = doc["anim"] | true;
    #else
        theme = doc["theme"] | 2;
        anim = doc["anim"] | true;
    #endif
    bool theme_migrated = doc["theme_migrated"] | false;
    needs_save = !theme_migrated;
    #if defined( LILYGO_T_DECK_PLUS )
    if ( theme < 0 || theme > 1 ) {
        theme = 1;
        needs_save = true;
    }
    #else
    if ( theme == 3 || theme < 0 || theme > 3 ) {
        theme = 2;
        needs_save = true;
    }
    #endif
    return true;
}

bool style_config_t::onDefault( void ) {
    #if defined( M5PAPER )
        theme = 0;
        anim = false;
    #elif defined( LILYGO_T_DECK_PLUS )
        theme = 1;
        anim = true;
    #else
        theme = 2;
        anim = true;
    #endif
    needs_save = false;
    return true;
}
