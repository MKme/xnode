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
#include <stdio.h>
#include "config.h"

#include "mainbar.h"
#include "main_tile/main_tile.h"
#include "setup_tile/setup_tile.h"
#include "note_tile/note_tile.h"
#include "app_tile/app_tile.h"
#include "gui/keyboard.h"
#include "gui/statusbar.h"
#include "gui/widget_styles.h"
#include "gui/gui.h"

#include "hardware/display.h"
#include "hardware/powermgm.h"
#include "hardware/rtcctl.h"
#include "hardware/button.h"
#include "hardware/touch.h"
#include "hardware/motor.h"

#include "utils/alloc.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

mainbar_history_t *mainbar_history = NULL;

static lv_obj_t *mainbar = NULL;

static lv_tile_t *tile = NULL;
static lv_point_t *tile_pos_table = NULL;
static uint32_t tile_entrys = 0;
static uint32_t app_tile_x_pos = MAINBAR_APP_TILE_X_START;
static uint32_t app_tile_y_pos = MAINBAR_APP_TILE_Y_START;
static volatile bool mainbar_alarm_occurred = false;

bool mainbar_button_event_cb( EventBits_t event, void *arg );
bool mainbar_powermgm_event_cb( EventBits_t event, void *arg );
bool mainbar_rtcctl_event_cb( EventBits_t event, void *arg );
void mainbar_add_current_tile_to_history( void );
#if defined( LILYGO_T_DECK_PRO )
static int32_t mainbar_find_tile_number( lv_coord_t x, lv_coord_t y );
static bool mainbar_tdeck_pro_jump_relative( lv_coord_t x_delta, lv_coord_t y_delta );
static void mainbar_tdeck_pro_get_tile_act( lv_coord_t *x, lv_coord_t *y );
static bool mainbar_tdeck_pro_set_tile_act( uint32_t tile_number );
static void mainbar_tdeck_pro_event_cb( lv_obj_t *obj, lv_event_t event );
static uint32_t mainbar_tdeck_pro_active_tile = 0;
#endif

void mainbar_setup( void ) {
    /*
     * check if mainbar already initialized
     */
    if ( mainbar ) {
        MAINBAR_ERROR_LOG("main already initialized");
        return;
    }

    mainbar_history = (mainbar_history_t*)MALLOC_ASSERT( sizeof( mainbar_history_t ), "error while alloc" );

#if defined( LILYGO_T_DECK_PRO )
    mainbar = lv_cont_create( lv_scr_act(), NULL );
    lv_obj_set_size( mainbar, lv_disp_get_hor_res( NULL ), LV_VER_RES );
    lv_obj_set_pos( mainbar, 0, 0 );
    lv_obj_set_drag( mainbar, false );
    lv_obj_set_drag_throw( mainbar, false );
    lv_obj_set_gesture_parent( mainbar, false );
    lv_obj_set_event_cb( mainbar, mainbar_tdeck_pro_event_cb );
#else
    mainbar = lv_tileview_create( lv_scr_act(), NULL);
    lv_tileview_set_edge_flash( mainbar, false);
#endif
    lv_obj_add_style( mainbar, LV_OBJ_PART_MAIN, ws_get_mainbar_style() );
#if !defined( LILYGO_T_DECK_PRO )
    lv_page_set_scrlbar_mode( mainbar, LV_SCRLBAR_MODE_OFF);
#endif
    powermgm_register_cb_with_prio( POWERMGM_STANDBY, mainbar_powermgm_event_cb, "mainbar powermgm", CALL_CB_FIRST );
    powermgm_register_cb_with_prio( POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP, mainbar_powermgm_event_cb, "mainbar powermgm", CALL_CB_LAST );
    rtcctl_register_cb( RTCCTL_ALARM_OCCURRED, mainbar_rtcctl_event_cb, "mainbar rtcctl" );
    button_register_cb( BUTTON_UP | BUTTON_RIGHT | BUTTON_LEFT | BUTTON_DOWN | BUTTON_ENTER | BUTTON_EXIT | BUTTON_REFRESH | BUTTON_SETUP | BUTTON_MENU | BUTTON_KEYBOARD, mainbar_button_event_cb, "mainbay button event" );

    mainbar_clear_history();
}

bool mainbar_button_event_cb( EventBits_t event, void *arg ) {
    lv_coord_t x,y;
    uint32_t current_tile = -1;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );
    /**
     * get the current tile number
     */
#if defined( LILYGO_T_DECK_PRO )
    mainbar_tdeck_pro_get_tile_act( &x, &y );
#else
    lv_tileview_get_tile_act( mainbar, &x, &y );
#endif
    for ( int i = 0 ; i < tile_entrys; i++ ) {
        if ( tile_pos_table[ i ].x == x && tile_pos_table[ i ].y == y ) {
            current_tile = i;
        }
    }
    /**
     * call button callback for the current tile if exist
     */
    if ( current_tile != -1 ) {
        if ( tile[ current_tile ].button_cb != NULL ) {
            MAINBAR_INFO_LOG("call button cb for tile: %d", current_tile );
            tile[ current_tile ].button_cb( event, arg );
        }
        else {
            MAINBAR_INFO_LOG("no button cb for current tile: %d", current_tile );
            if ( event == BUTTON_EXIT ) {
                mainbar_jump_back();
            }
        }
    }
    /**
     * trigger activity
     */
    display_note_activity();
    
    return( true );
}

void mainbar_add_current_tile_to_history( lv_anim_enable_t anim ) {
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    if ( mainbar_history->entrys < MAINBAR_MAX_HISTORY ) {
        /**
         * get current tile
         */
        lv_coord_t x,y;
#if defined( LILYGO_T_DECK_PRO )
        mainbar_tdeck_pro_get_tile_act( &x, &y );
#else
        lv_tileview_get_tile_act( mainbar, &x, &y );
#endif
        /**
         * store tile pos in history
         */
        mainbar_history->entrys++;
        mainbar_history->tile[ mainbar_history->entrys ].x = x;
        mainbar_history->tile[ mainbar_history->entrys ].y = y;
        mainbar_history->statusbar[ mainbar_history->entrys ] = statusbar_get_hidden_state();
        mainbar_history->anim[ mainbar_history->entrys ] = anim;
        mainbar_history->powermgm_state[ mainbar_history->entrys ] = powermgm_get_event( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP );
        MAINBAR_INFO_LOG("store tile to history: %d, %d, %d, %x, %d", x, y, statusbar_get_hidden_state(), powermgm_get_event( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP ), anim );
    }
}

void mainbar_clear_history( void ) {
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    mainbar_history->entrys = 0;
    mainbar_history->tile[ 0 ].x = 0;
    mainbar_history->tile[ 0 ].y = 0;
    mainbar_history->statusbar[ 0 ] = true;
    mainbar_history->powermgm_state[ 0 ] = 0;
    MAINBAR_INFO_LOG("clear mainbar history");
}

void mainbar_jump_back( void ) {
    lv_coord_t x,y;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    if ( mainbar_history->entrys > 0 && mainbar_history->entrys < MAINBAR_MAX_HISTORY ) {
        /**
         * get the current tile pos for later use
         */
#if defined( LILYGO_T_DECK_PRO )
        mainbar_tdeck_pro_get_tile_act( &x, &y );
#else
        lv_tileview_get_tile_act( mainbar, &x, &y );
#endif
        /**
         * jump back
         */
        MAINBAR_INFO_LOG("jump back to tile: %d, %d, %d", mainbar_history->tile[ mainbar_history->entrys ].x, mainbar_history->tile[ mainbar_history->entrys ].y, mainbar_history->statusbar[ mainbar_history->entrys ] );
#if defined( LILYGO_T_DECK_PRO )
        int32_t back_tile_number = mainbar_find_tile_number( mainbar_history->tile[ mainbar_history->entrys ].x, mainbar_history->tile[ mainbar_history->entrys ].y );
        if ( back_tile_number >= 0 ) {
            mainbar_tdeck_pro_set_tile_act( (uint32_t)back_tile_number );
        }
#else
        lv_tileview_set_tile_act( mainbar, mainbar_history->tile[ mainbar_history->entrys ].x, mainbar_history->tile[ mainbar_history->entrys ].y, mainbar_history->anim[ mainbar_history->entrys ] );
#endif
        statusbar_hide( mainbar_history->statusbar[ mainbar_history->entrys ] );
        gui_force_redraw( true );
        /**
         * restore powermgm state
         */
        switch( mainbar_history->powermgm_state[ mainbar_history->entrys ] ) {
            case( POWERMGM_STANDBY ):
                powermgm_set_event( POWERMGM_STANDBY_REQUEST );
                MAINBAR_INFO_LOG("mainbar send standby request");
                break;
            case( POWERMGM_WAKEUP ):
                powermgm_set_event( POWERMGM_WAKEUP_REQUEST );
                MAINBAR_INFO_LOG("mainbar send wakeup request");
                break;
            case( POWERMGM_SILENCE_WAKEUP ):
                powermgm_set_event( POWERMGM_SILENCE_WAKEUP_REQUEST );
                MAINBAR_INFO_LOG("mainbar send silence wakeup request");
                break;
            default:
                break;
        }
        /**
         * search for the hibernate cb
         */
        for ( int tile_number = 0 ; tile_number < tile_entrys; tile_number++ ) {
            if ( tile_pos_table[ tile_number ].x == x && tile_pos_table[ tile_number ].y == y ) {
                /**
                 * call hibernate callback for the current tile if exist
                 */
                if ( tile[ tile_number ].hibernate_cb != NULL ) {
                    for( int i = 0 ; i < tile[ tile_number ].hibernate_cb_entry_count ; i++ ) {
                        if ( tile[ tile_number ].hibernate_cb[ i ] != NULL ) {
                            MAINBAR_INFO_LOG("call hibernate cb [%d] for tile: %d", i, tile_number );                            
                            tile[ tile_number ].hibernate_cb[ i ]();
                        }
                    }
                }
            }
        }
        /**
         * search for the activation cb
         */
        for ( int tile_number = 0 ; tile_number < tile_entrys; tile_number++ ) {
            if ( tile_pos_table[ tile_number ].x == mainbar_history->tile[ mainbar_history->entrys ].x && tile_pos_table[ tile_number ].y == mainbar_history->tile[ mainbar_history->entrys ].y ) {
                /**
                 * call hibernate callback for the current tile if exist
                 */
                if ( tile[ tile_number ].activate_cb != NULL ) {
                    for( int i = 0 ; i < tile[ tile_number ].activate_cb_entry_count ; i++ ) {
                        if ( tile[ tile_number ].activate_cb[ i ] != NULL ) {
                            MAINBAR_INFO_LOG("call activation cb [%d] for tile: %d", i, tile_number );
                            tile[ tile_number ].activate_cb[ i ]();
                        }
                    }
                }
            }
        }
        mainbar_history->entrys--;
    }
    else {
        mainbar_jump_to_maintile( LV_ANIM_OFF );
    }
}

bool mainbar_rtcctl_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case RTCCTL_ALARM_OCCURRED:
            mainbar_alarm_occurred = true;
            break;
    }
    return( true );
}

bool mainbar_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:
            if ( !mainbar_alarm_occurred ) {
                if ( !display_get_block_return_maintile() ) {
                    mainbar_jump_to_maintile( LV_ANIM_OFF );
                }
            }
            break;
        case POWERMGM_SILENCE_WAKEUP:
            mainbar_alarm_occurred = false;
            break;
        case POWERMGM_WAKEUP:
            break;
    }
    return( true );
}

uint32_t mainbar_add_tile( uint16_t x, uint16_t y, const char *id, lv_style_t *style ) {
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    tile_entrys++;

    if ( tile_pos_table == NULL ) {
        tile_pos_table = ( lv_point_t * )MALLOC_ASSERT( sizeof( lv_point_t ) * tile_entrys, "tile_pos_table malloc faild" );
        tile = ( lv_tile_t * )MALLOC_ASSERT( sizeof( lv_tile_t ) * tile_entrys, "tile malloc faild" );
    }
    else {
        tile_pos_table = ( lv_point_t * )REALLOC_ASSERT( tile_pos_table, sizeof( lv_point_t ) * tile_entrys, "tile_pos_table realloc faild" );
        tile = ( lv_tile_t * )REALLOC_ASSERT( tile, sizeof( lv_tile_t ) * tile_entrys, "tile realloc faild" );
    }

    tile_pos_table[ tile_entrys - 1 ].x = x;
    tile_pos_table[ tile_entrys - 1 ].y = y;

    lv_obj_t *my_tile = lv_cont_create( mainbar, NULL);  
    tile[ tile_entrys - 1 ].tile = my_tile;
    tile[ tile_entrys - 1 ].activate_cb_entry_count = 0;
    tile[ tile_entrys - 1 ].activate_cb = NULL;
    tile[ tile_entrys - 1 ].hibernate_cb_entry_count = 0;
    tile[ tile_entrys - 1 ].hibernate_cb = NULL;
    tile[ tile_entrys - 1 ].button_cb = NULL;
    tile[ tile_entrys - 1 ].x = x;
    tile[ tile_entrys - 1 ].y = y;
    tile[ tile_entrys - 1 ].id = id;
    lv_obj_set_size( tile[ tile_entrys - 1 ].tile, lv_disp_get_hor_res( NULL ), LV_VER_RES);
    lv_obj_add_style( tile[ tile_entrys - 1 ].tile, LV_OBJ_PART_MAIN, style );
#if defined( LILYGO_T_DECK_PRO )
    if ( tile_entrys == 1 ) {
        mainbar_tdeck_pro_active_tile = 0;
    }
    lv_obj_set_pos( tile[ tile_entrys - 1 ].tile, 0, 0 );
    lv_obj_set_drag( tile[ tile_entrys - 1 ].tile, false );
    lv_obj_set_drag_throw( tile[ tile_entrys - 1 ].tile, false );
    lv_obj_set_hidden( tile[ tile_entrys - 1 ].tile, ( tile_entrys - 1 ) != mainbar_tdeck_pro_active_tile );
#else
    lv_obj_set_pos( tile[ tile_entrys - 1 ].tile, tile_pos_table[ tile_entrys - 1 ].x * lv_disp_get_hor_res( NULL ) , tile_pos_table[ tile_entrys - 1 ].y * LV_VER_RES );
    lv_tileview_add_element( mainbar, tile[ tile_entrys - 1 ].tile );
    lv_tileview_set_valid_positions( mainbar, tile_pos_table, tile_entrys );
#endif
    MAINBAR_INFO_LOG("add tile: x=%d, y=%d, id=%s", tile_pos_table[ tile_entrys - 1 ].x, tile_pos_table[ tile_entrys - 1 ].y, tile[ tile_entrys - 1 ].id );

    return( tile_entrys - 1 );
}

bool mainbar_add_tile_hibernate_cb( uint32_t tile_number, MAINBAR_CALLBACK_FUNC hibernate_cb ) {
    bool retval = false;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    if ( tile_number < tile_entrys ) {
        tile[ tile_number ].hibernate_cb_entry_count++;

        if( tile[ tile_number ].hibernate_cb )
            tile[ tile_number ].hibernate_cb = (MAINBAR_CALLBACK_FUNC*)REALLOC_ASSERT( tile[ tile_number ].hibernate_cb, sizeof( MAINBAR_CALLBACK_FUNC* ) * tile[ tile_number ].hibernate_cb_entry_count, "maintile hibernate_cb reallocation failed" );
        else
            tile[ tile_number ].hibernate_cb = (MAINBAR_CALLBACK_FUNC*)MALLOC_ASSERT( sizeof( MAINBAR_CALLBACK_FUNC* ), "maintile hibernate_cb allocation failed" );

        tile[ tile_number ].hibernate_cb[ tile[ tile_number ].hibernate_cb_entry_count - 1 ] = hibernate_cb;
        
        retval = true;
    }
    else
        MAINBAR_ERROR_LOG("tile number %d do not exist", tile_number );

    return( retval );
}

bool mainbar_add_tile_activate_cb( uint32_t tile_number, MAINBAR_CALLBACK_FUNC activate_cb ) {
    bool retval = false;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );
        
    if ( tile_number < tile_entrys ) {
        tile[ tile_number ].activate_cb_entry_count++;

        if( tile[ tile_number ].activate_cb )
            tile[ tile_number ].activate_cb = (MAINBAR_CALLBACK_FUNC*)REALLOC_ASSERT( tile[ tile_number ].activate_cb, sizeof(MAINBAR_CALLBACK_FUNC*) * tile[ tile_number ].activate_cb_entry_count, "maintile hibernate_cb reallocation failed" );
        else
            tile[ tile_number ].activate_cb = (MAINBAR_CALLBACK_FUNC*)MALLOC_ASSERT(sizeof(MAINBAR_CALLBACK_FUNC*), "maintile hibernate_cb allocation failed" );

        tile[ tile_number ].activate_cb[ tile[ tile_number ].activate_cb_entry_count - 1 ] = activate_cb;

        retval = true;
    }
    else
        MAINBAR_ERROR_LOG("tile number %d do not exist", tile_number );

    return( retval );
}

bool mainbar_add_tile_button_cb( uint32_t tile_number, CALLBACK_FUNC button_cb ) {
    bool retval = false;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    if ( tile_number < tile_entrys ) {
        tile[ tile_number ].button_cb = button_cb;
        retval = true;
    }
    else
        MAINBAR_ERROR_LOG("tile number %d do not exist", tile_number );

    return( retval );
}

uint32_t mainbar_add_app_tile( uint16_t x, uint16_t y, const char *id ) {
    uint32_t retval = -1;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );
    /*
     * prevent tile x pos goes out of range ( uint16_t )
     */
    if( ( app_tile_x_pos + x ) * lv_disp_get_hor_res( NULL ) > 32000 ) {
        MAINBAR_INFO_LOG("max horz resolution, jump next vert line");
        app_tile_x_pos = 0;
        app_tile_y_pos = app_tile_y_pos + MAINBAR_APP_TILE_Y_START;
    }
    /**
     * crawl tiles
     */
    for ( int hor = 0 ; hor < x ; hor++ ) {
        for ( int ver = 0 ; ver < y ; ver++ ) {
            if ( retval == -1 )
                retval = mainbar_add_tile( hor + app_tile_x_pos, app_tile_y_pos + ver + MAINBAR_APP_TILE_Y_START, id, ws_get_app_style() );
            else
                mainbar_add_tile( hor + app_tile_x_pos, app_tile_y_pos + ver + MAINBAR_APP_TILE_Y_START, id, ws_get_app_style() );
        }
    }

    app_tile_x_pos = app_tile_x_pos + x + 1;

    return( retval );
}

uint32_t mainbar_add_setup_tile( uint16_t x, uint16_t y, const char *id ) {
    uint32_t retval = -1;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );
    /*
     * prevent tile x pos goes out of range ( uint16_t )
     */
    if( ( app_tile_x_pos + x ) * lv_disp_get_hor_res( NULL ) > 32000 ) {
        MAINBAR_INFO_LOG("max horz resolution, jump next vert line");
        app_tile_x_pos = 0;
        app_tile_y_pos = app_tile_y_pos + MAINBAR_APP_TILE_Y_START;
    }
    /**
     * crawl tiles
     */
    for ( int hor = 0 ; hor < x ; hor++ ) {
        for ( int ver = 0 ; ver < y ; ver++ ) {
            if ( retval == -1 )
                retval = mainbar_add_tile( hor + app_tile_x_pos, app_tile_y_pos + ver + MAINBAR_APP_TILE_Y_START, id, ws_get_setup_tile_style() );
            else
                mainbar_add_tile( hor + app_tile_x_pos, app_tile_y_pos + ver + MAINBAR_APP_TILE_Y_START, id, ws_get_setup_tile_style() );
        }
    }

    app_tile_x_pos = app_tile_x_pos + x + 1;

    return( retval );
}

lv_obj_t *mainbar_get_tile_obj( uint32_t tile_number ) {
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    if ( tile_number < tile_entrys )
        return( tile[ tile_number ].tile );
    else
        MAINBAR_ERROR_LOG( "tile number %d do not exist", tile_number );

    return( NULL );
}

void mainbar_jump_to_maintile( lv_anim_enable_t anim ) {
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    if ( tile_entrys != 0 ) {
        mainbar_jump_to_tilenumber( 0, anim );
        keyboard_hide();
        statusbar_hide( false );
        statusbar_expand( false );
        mainbar_clear_history();
    }
    else {
        MAINBAR_ERROR_LOG( "main tile do not exist" );
    }
}

void mainbar_jump_to_tilenumber( uint32_t tile_number, lv_anim_enable_t anim, bool statusbar ) {
    lv_coord_t x,y;
    uint32_t current_tile = 0;
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );
    /**
     * get the current tile number
     */
#if defined( LILYGO_T_DECK_PRO )
    if ( mainbar_tdeck_pro_active_tile < tile_entrys ) {
        current_tile = mainbar_tdeck_pro_active_tile;
        x = tile_pos_table[ current_tile ].x;
        y = tile_pos_table[ current_tile ].y;
        if ( current_tile == tile_number ) {
            MAINBAR_INFO_LOG("the destination tile is the current tile");
            return;
        }
    }
#else
    lv_tileview_get_tile_act( mainbar, &x, &y );
    for ( int i = 0 ; i < tile_entrys; i++ ) {
        if ( tile_pos_table[ i ].x == x && tile_pos_table[ i ].y == y ) {
            current_tile = i;
            /**
             * ignore tile jump if we a on destination
             */
            if( current_tile == tile_number ) {
                MAINBAR_INFO_LOG("the destination tile is the current tile");
                return;
            }
        }
    }
#endif
    /**
     * check if tile alread in mainbar history to prevent loops
     */
    for ( int i = 0 ; i < mainbar_history->entrys; i++ ) {
        if ( mainbar_history->tile[ i ].x == x && mainbar_history->tile[ i ].y == y ) {
            MAINBAR_INFO_LOG("current tile already in mainbar_history");
            return;
        }
    }
    /**
     * jump
     */
    if ( tile_number < tile_entrys ) {
        /**
         * store current tile and statusbar state
         */
        mainbar_add_current_tile_to_history( anim );
        statusbar_hide( statusbar );
        /**
         * jump into tile
         */
        MAINBAR_INFO_LOG("jump to tile %d from tile %d", tile_number, current_tile );
#if defined( LILYGO_T_DECK_PRO )
        mainbar_tdeck_pro_set_tile_act( tile_number );
#else
        lv_tileview_set_tile_act( mainbar, tile_pos_table[ tile_number ].x, tile_pos_table[ tile_number ].y, anim );
#endif
        gui_force_redraw( true );
        /**
         * call hibernate callback for the current tile if exist
         */
        if ( tile[ current_tile ].hibernate_cb != NULL ) {
            for( int i = 0 ; i < tile[ current_tile ].hibernate_cb_entry_count ; i++ ) {
                if ( tile[ current_tile ].hibernate_cb[ i ] != NULL ) {
                    MAINBAR_INFO_LOG("call hibernate cb [%d] for tile: %d", i, tile_number );
                    tile[ current_tile ].hibernate_cb[ i ]();
                }
            }
        }
        /**
         * call activate callback for the new tile if exist
         */
        if ( tile[ tile_number ].activate_cb != NULL ) {
            for( int i = 0 ; i < tile[ tile_number ].activate_cb_entry_count ; i++ ) {
                if ( tile[ tile_number ].activate_cb[ i ] != NULL ) {
                    MAINBAR_INFO_LOG("call activation cb [%d] for tile: %d", i, tile_number );
                    tile[ tile_number ].activate_cb[ i ]();
                }
            }
        }
    }
    else {
        MAINBAR_ERROR_LOG( "tile number %d do not exist", tile_number );
    }    
}

void mainbar_jump_to_tilenumber( uint32_t tile_number, lv_anim_enable_t anim ) {
    mainbar_jump_to_tilenumber( tile_number, anim, statusbar_get_hidden_state() );
}

lv_obj_t * mainbar_obj_create(lv_obj_t *parent) {
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "main not initialized" );

    lv_obj_t * child = lv_obj_create( parent, NULL );
#if !defined( LILYGO_T_DECK_PRO )
    lv_tileview_add_element( mainbar, child );
#endif

    return child;
}

void mainbar_add_slide_element(lv_obj_t *element) {
    /*
     * check if mainbar already initialized
     */
    ASSERT( mainbar, "mainbar not initialized" );

#if !defined( LILYGO_T_DECK_PRO )
    lv_tileview_add_element( mainbar, element );
#else
    (void)element;
#endif
}

#if defined( LILYGO_T_DECK_PRO )
static void mainbar_tdeck_pro_event_cb( lv_obj_t *obj, lv_event_t event ) {
    (void)obj;

    if ( event != LV_EVENT_GESTURE ) {
        return;
    }

    switch ( lv_indev_get_gesture_dir( lv_indev_get_act() ) ) {
        case LV_GESTURE_DIR_LEFT:
            mainbar_tdeck_pro_jump_relative( 1, 0 );
            break;
        case LV_GESTURE_DIR_RIGHT:
            mainbar_tdeck_pro_jump_relative( -1, 0 );
            break;
        case LV_GESTURE_DIR_TOP:
            mainbar_tdeck_pro_jump_relative( 0, 1 );
            break;
        case LV_GESTURE_DIR_BOTTOM:
            mainbar_tdeck_pro_jump_relative( 0, -1 );
            break;
        default:
            break;
    }

    lv_indev_reset( NULL, NULL );
    gui_force_redraw( true );
}

static void mainbar_tdeck_pro_get_tile_act( lv_coord_t *x, lv_coord_t *y ) {
    if ( tile_entrys == 0 || mainbar_tdeck_pro_active_tile >= tile_entrys ) {
        if ( x != NULL ) {
            *x = 0;
        }
        if ( y != NULL ) {
            *y = 0;
        }
        return;
    }

    if ( x != NULL ) {
        *x = tile_pos_table[ mainbar_tdeck_pro_active_tile ].x;
    }
    if ( y != NULL ) {
        *y = tile_pos_table[ mainbar_tdeck_pro_active_tile ].y;
    }
}

static bool mainbar_tdeck_pro_set_tile_act( uint32_t tile_number ) {
    if ( tile_number >= tile_entrys ) {
        return( false );
    }

    if ( mainbar_tdeck_pro_active_tile < tile_entrys ) {
        lv_obj_set_hidden( tile[ mainbar_tdeck_pro_active_tile ].tile, true );
    }

    mainbar_tdeck_pro_active_tile = tile_number;
    lv_obj_set_pos( tile[ tile_number ].tile, 0, 0 );
    lv_obj_set_hidden( tile[ tile_number ].tile, false );
    lv_obj_invalidate( lv_scr_act() );
    return( true );
}

static int32_t mainbar_find_tile_number( lv_coord_t x, lv_coord_t y ) {
    for ( uint32_t i = 0; i < tile_entrys; i++ ) {
        if ( tile_pos_table[ i ].x == x && tile_pos_table[ i ].y == y ) {
            return( (int32_t)i );
        }
    }
    return( -1 );
}

static bool mainbar_tdeck_pro_jump_relative( lv_coord_t x_delta, lv_coord_t y_delta ) {
    if ( mainbar == NULL || tile_entrys == 0 ) {
        return( false );
    }

    static uint32_t last_jump_ms = 0;
    const uint32_t now_ms = millis();
    if ( now_ms - last_jump_ms < 250 ) {
        return( false );
    }

    lv_coord_t x = 0;
    lv_coord_t y = 0;
    mainbar_tdeck_pro_get_tile_act( &x, &y );

    int32_t tile_number = mainbar_find_tile_number( x + x_delta, y + y_delta );
    if ( tile_number < 0 ) {
        MAINBAR_INFO_LOG( "T-Deck PRO gesture ignored: no tile at %d,%d", x + x_delta, y + y_delta );
        Serial.printf( "T-Deck PRO gesture ignored: %d,%d -> %d,%d\r\n", x, y, x + x_delta, y + y_delta );
        return( false );
    }

    MAINBAR_INFO_LOG( "T-Deck PRO gesture jump: %d,%d -> %d,%d", x, y, x + x_delta, y + y_delta );
    Serial.printf( "T-Deck PRO gesture jump: %d,%d -> %d,%d\r\n", x, y, x + x_delta, y + y_delta );
    mainbar_jump_to_tilenumber( (uint32_t)tile_number, LV_ANIM_OFF );
    mainbar_clear_history();
    lv_indev_reset( NULL, NULL );
    gui_force_redraw( true );
    lv_obj_invalidate( lv_scr_act() );
    last_jump_ms = now_ms;
    return( true );
}

bool mainbar_poll_tdeck_pro_gesture( void ) {
    static bool tracking = false;
    static lv_point_t start_point = { 0, 0 };
    static lv_point_t last_point = { 0, 0 };
    static uint32_t last_touch_ms = 0;

    constexpr lv_coord_t swipe_threshold = 54;
    constexpr uint32_t release_grace_ms = 140;

    int8_t x_delta = 0;
    int8_t y_delta = 0;
    if ( touch_get_swipe_delta( &x_delta, &y_delta ) ) {
        if ( mainbar_tdeck_pro_jump_relative( x_delta, y_delta ) ) {
            Serial.printf( "T-Deck PRO touch swipe: %d,%d\r\n", x_delta, y_delta );
        }
        lv_indev_reset( NULL, NULL );
        gui_force_redraw( true );
        return( true );
    }

    touch_t touch;
    if ( !touch_get_last( touch ) || !touch.touched ) {
        if ( !tracking ) {
            return( false );
        }

        const uint32_t now_ms = millis();
        if ( now_ms - last_touch_ms < release_grace_ms ) {
            return( true );
        }

        tracking = false;

        if ( start_point.y <= STATUSBAR_HEIGHT ) {
            return( false );
        }

        const lv_coord_t dx = last_point.x - start_point.x;
        const lv_coord_t dy = last_point.y - start_point.y;
        const lv_coord_t abs_x = dx < 0 ? -dx : dx;
        const lv_coord_t abs_y = dy < 0 ? -dy : dy;

        if ( abs_x >= swipe_threshold && abs_x > abs_y ) {
            mainbar_tdeck_pro_jump_relative( dx < 0 ? 1 : -1, 0 );
            lv_indev_reset( NULL, NULL );
            return( true );
        }

        if ( abs_y >= swipe_threshold && abs_y > abs_x ) {
            mainbar_tdeck_pro_jump_relative( 0, dy < 0 ? 1 : -1 );
            lv_indev_reset( NULL, NULL );
            return( true );
        }

        return( false );
    }

    if ( !tracking ) {
        start_point.x = touch.x_coor;
        start_point.y = touch.y_coor;
        last_point = start_point;
        tracking = true;
    }
    else {
        last_point.x = touch.x_coor;
        last_point.y = touch.y_coor;
    }

    last_touch_ms = millis();
    return( true );
}
#endif
