/****************************************************************************
 *   Aug 3 12:17:11 2020
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
#ifndef _OSM_APP_MAIN_H
    #define _OSM_APP_MAIN_H

    #include "config.h"

    #ifdef NATIVE_64BIT
        #include "utils/io.h"
    #endif

    #define OSMMAP_TASK_STACK_SIZE              5000

    #define OSM_APP_UPDATE_REQUEST              _BV(0)      /** @brief set tile image update flag */
    #define OSM_APP_LOAD_AHEAD_REQUEST          _BV(1)      /** @brief set tile image update flag */
    #define OSM_APP_TASK_EXIT_REQUEST           _BV(2)      /** @brief set task exit flag */
    /**
     * @brief osmmap app main setup routine
     * 
     * @param tile_num 
     */
    void osmmap_app_main_setup( uint32_t tile_num );
    void osmmap_set_external_marker( double lon, double lat, const char *label );
    void osmmap_clear_external_marker( void );
    void osmmap_upsert_overlay_item( const char *key, const char *kind, double lon, double lat, const char *label, uint32_t updated_at );
    void osmmap_clear_overlay_items( void );
    uint32_t osmmap_overlay_item_count( void );
    bool osmmap_apply_watch_basemap( const char *map_name, double lon, double lat, uint32_t zoom );

#endif // _OSM_APP_MAIN_H
