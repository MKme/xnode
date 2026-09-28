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
static bool mainbar_menus_finalized = false;
static uint16_t mainbar_used_app_pages = 0;
static uint16_t mainbar_used_setup_pages = 0;
#if defined(LILYGO_T_DECK_PRO)
static uint32_t mainbar_tdeck_pro_active_tile = 0;
#endif
#if !defined(LILYGO_T_DECK_PRO)
static uint32_t mainbar_last_active_tile = 0;
static bool mainbar_programmatic_navigation = false;
static void mainbar_swipe_event(lv_obj_t *obj, lv_event_t event);
#endif
LV_FONT_DECLARE(Ubuntu_12px);

static int32_t mainbar_find_tile_at(lv_coord_t x, lv_coord_t y) {
    for (uint32_t i = 0; i < tile_entrys; ++i)
        if (tile_pos_table[i].x == x && tile_pos_table[i].y == y) return (int32_t)i;
    return -1;
}

void mainbar_add_page_hint(lv_obj_t *parent, const char *section, uint16_t page, uint16_t count) {
    lv_obj_t *hint = lv_label_create(parent, NULL);
    lv_obj_reset_style_list(hint, LV_OBJ_PART_MAIN);
    lv_obj_add_style(hint, LV_OBJ_PART_MAIN, ws_get_label_style());
    lv_obj_set_style_local_text_font(hint, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &Ubuntu_12px);
    char text[40];
    snprintf(text, sizeof(text), "<  %s %u/%u  >", section, page, count);
    lv_label_set_text(hint, text);
    lv_obj_set_click(hint, false);
    lv_obj_align(hint, parent, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    for (uint32_t i = 0; i < tile_entrys; ++i)
        if (tile[i].tile == parent) { tile[i].page_hint = hint; break; }
}

static void mainbar_reposition_menu(uint32_t number, uint16_t x, uint16_t y,
                                  const char *section, uint16_t page, uint16_t count) {
    tile_pos_table[number] = {(lv_coord_t)x, (lv_coord_t)y};
    tile[number].x = x;
    tile[number].y = y;
#if !defined(LILYGO_T_DECK_PRO)
    lv_obj_set_pos(tile[number].tile, x * lv_disp_get_hor_res(NULL), y * LV_VER_RES);
#endif
    if (tile[number].page_hint) {
        char text[40];
        snprintf(text, sizeof(text), "<  %s %u/%u  >", section, page, count);
        lv_label_set_text(tile[number].page_hint, text);
        lv_obj_align(tile[number].page_hint, tile[number].tile, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    }
}

static void mainbar_reflow_menu_pages(void) {
    // Preserve destinations by ID while root coordinates move around newly filled pages.
    const uint16_t apps = app_tile_get_used_pages();
    const uint16_t setups = setup_tile_get_used_pages();
    if (apps == mainbar_used_app_pages && setups == mainbar_used_setup_pages) return;
    int32_t history_ids[MAINBAR_MAX_HISTORY];
    for (uint32_t i = 0; i <= mainbar_history->entrys; ++i)
        history_ids[i] = mainbar_find_tile_at(mainbar_history->tile[i].x, mainbar_history->tile[i].y);
#if defined(LILYGO_T_DECK_PRO)
    const int32_t active = (int32_t)mainbar_tdeck_pro_active_tile;
#else
    lv_coord_t active_x, active_y;
    lv_tileview_get_tile_act(mainbar, &active_x, &active_y);
    const int32_t active = mainbar_find_tile_at(active_x, active_y);
    mainbar_programmatic_navigation = true;
#endif
    const uint32_t first_app = app_tile_get_tile_num();
    const uint32_t first_setup = setup_get_tile_num();
    for (uint16_t i = 0; i < MAX_APPS_TILES; ++i)
        mainbar_reposition_menu(first_app + i, i < apps ? 1 + i : i,
                                i < apps ? 0 : MAINBAR_APP_TILE_Y_START, "APPS", i + 1, apps);
    for (uint16_t i = 0; i < MAX_SETUP_TILES; ++i)
        mainbar_reposition_menu(first_setup + i, i < setups ? 1 + apps + i : MAX_APPS_TILES + i,
                                i < setups ? 0 : MAINBAR_APP_TILE_Y_START, "SETUP", i + 1, setups);
    mainbar_reposition_menu(note_tile_get_tile_num(), 1 + apps + setups, 0, "NOTES", 1, 1);
    for (uint32_t i = 0; i <= mainbar_history->entrys; ++i)
        if (history_ids[i] >= 0) mainbar_history->tile[i] = tile_pos_table[history_ids[i]];
#if !defined(LILYGO_T_DECK_PRO)
    lv_tileview_set_valid_positions(mainbar, tile_pos_table, tile_entrys);
    if (active >= 0) {
        lv_tileview_set_tile_act(mainbar, tile_pos_table[active].x, tile_pos_table[active].y, LV_ANIM_OFF);
        mainbar_last_active_tile = (uint32_t)active;
    }
    mainbar_programmatic_navigation = false;
#endif
    mainbar_used_app_pages = apps;
    mainbar_used_setup_pages = setups;
}

void mainbar_finalize_menu_pages(void) {
    mainbar_reflow_menu_pages();
    mainbar_menus_finalized = true;
}

void mainbar_menu_registration_changed(void) {
    // App constructors register before the root menu is complete; defer those to finalize.
    if (mainbar_menus_finalized) mainbar_reflow_menu_pages();
}

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
    lv_obj_set_event_cb(mainbar, mainbar_swipe_event);
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
    if (y == 0 && (event == BUTTON_LEFT || event == BUTTON_RIGHT)) {
        const int32_t next = mainbar_find_tile_at(x + (event == BUTTON_RIGHT ? 1 : -1), 0);
        if (next >= 0) {
            mainbar_jump_to_tilenumber((uint32_t)next, LV_ANIM_OFF);
            mainbar_clear_history();
        }
        display_note_activity();
        return true;
    }
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
            else if (event == BUTTON_LEFT || event == BUTTON_RIGHT) {
                const int32_t next = mainbar_find_tile_at(x + (event == BUTTON_RIGHT ? 1 : -1), y);
                if (next >= 0) mainbar_jump_to_tilenumber((uint32_t)next, LV_ANIM_OFF);
            }
        }
    }
    /**
     * trigger activity
     */
    display_note_activity();
    
    return( true );
}

static void mainbar_store_history(lv_coord_t x, lv_coord_t y, lv_anim_enable_t anim) {
    // Slot zero is the home sentinel. Keep the most recent fifteen entries;
    // the former increment-before-write overflowed at MAINBAR_MAX_HISTORY.
    if (mainbar_history->entrys >= MAINBAR_MAX_HISTORY - 1) {
        for (uint32_t i = 1; i < MAINBAR_MAX_HISTORY - 1; ++i) {
            mainbar_history->tile[i] = mainbar_history->tile[i + 1];
            mainbar_history->statusbar[i] = mainbar_history->statusbar[i + 1];
            mainbar_history->anim[i] = mainbar_history->anim[i + 1];
            mainbar_history->powermgm_state[i] = mainbar_history->powermgm_state[i + 1];
        }
        mainbar_history->entrys = MAINBAR_MAX_HISTORY - 2;
    }
    const uint32_t entry = ++mainbar_history->entrys;
    mainbar_history->tile[entry] = {x, y};
    mainbar_history->statusbar[entry] = statusbar_get_hidden_state();
    mainbar_history->anim[entry] = anim;
    mainbar_history->powermgm_state[entry] = powermgm_get_event(POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP);
}

void mainbar_add_current_tile_to_history(lv_anim_enable_t anim) {
    ASSERT(mainbar, "main not initialized");
    lv_coord_t x, y;
#if defined(LILYGO_T_DECK_PRO)
    mainbar_tdeck_pro_get_tile_act(&x, &y);
#else
    lv_tileview_get_tile_act(mainbar, &x, &y);
#endif
    mainbar_store_history(x, y, anim);
}

#if !defined(LILYGO_T_DECK_PRO)
static void mainbar_swipe_event(lv_obj_t *obj, lv_event_t event) {
    if (event != LV_EVENT_VALUE_CHANGED || mainbar_programmatic_navigation || !tile_entrys) return;
    lv_coord_t x, y;
    lv_tileview_get_tile_act(obj, &x, &y);
    const int32_t next = mainbar_find_tile_at(x, y);
    if (next < 0 || (uint32_t)next == mainbar_last_active_tile) return;
    const uint32_t previous = mainbar_last_active_tile;
    mainbar_last_active_tile = (uint32_t)next;
    if (y == 0) mainbar_clear_history();
    else mainbar_store_history(tile_pos_table[previous].x, tile_pos_table[previous].y, LV_ANIM_OFF);
    // Drag navigation must run the same lifecycle hooks as an explicit jump.
    for (int i = 0; i < tile[previous].hibernate_cb_entry_count; ++i)
        if (tile[previous].hibernate_cb[i]) tile[previous].hibernate_cb[i]();
    for (int i = 0; i < tile[next].activate_cb_entry_count; ++i)
        if (tile[next].activate_cb[i]) tile[next].activate_cb[i]();
    display_note_activity();
}
#endif

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
        mainbar_programmatic_navigation = true;
        lv_tileview_set_tile_act( mainbar, mainbar_history->tile[ mainbar_history->entrys ].x, mainbar_history->tile[ mainbar_history->entrys ].y, mainbar_history->anim[ mainbar_history->entrys ] );
        mainbar_last_active_tile = mainbar_find_tile_at(mainbar_history->tile[mainbar_history->entrys].x, mainbar_history->tile[mainbar_history->entrys].y);
        mainbar_programmatic_navigation = false;
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
    tile[ tile_entrys - 1 ].page_hint = NULL;
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

// Private application/setup groups are horizontal strips. Preserve the former
// hor-then-ver tile-ID order; an empty column separates unrelated groups.
static uint32_t mainbar_add_horizontal_group(uint16_t x, uint16_t y, const char *id, lv_style_t *style) {
    ASSERT(mainbar, "main not initialized");
    const uint32_t count = (uint32_t)x * y;
    const uint32_t width = lv_disp_get_hor_res(NULL);
    if (!count || count > 32000 / width) {
        MAINBAR_ERROR_LOG("invalid horizontal tile group: %u x %u", x, y);
        return (uint32_t)-1;
    }
    if ((app_tile_x_pos + count) * width > 32000) {
        app_tile_x_pos = 0;
        app_tile_y_pos += MAINBAR_APP_TILE_Y_START;
    }
    if ((app_tile_y_pos + MAINBAR_APP_TILE_Y_START + 1) * (uint32_t)LV_VER_RES > 32000) {
        MAINBAR_ERROR_LOG("horizontal tile rows exceed LVGL coordinate range");
        return (uint32_t)-1;
    }
    const uint32_t first = tile_entrys;
    for (uint32_t page = 0; page < count; ++page) {
        mainbar_add_tile(app_tile_x_pos + page, app_tile_y_pos + MAINBAR_APP_TILE_Y_START, id, style);
    }
    app_tile_x_pos += count + 1;
    return first;
}

uint32_t mainbar_add_app_tile(uint16_t x, uint16_t y, const char *id) {
    return mainbar_add_horizontal_group(x, y, id, ws_get_app_style());
}

uint32_t mainbar_add_setup_tile(uint16_t x, uint16_t y, const char *id) {
    return mainbar_add_horizontal_group(x, y, id, ws_get_setup_tile_style());
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
    // Revisiting a prior page is valid navigation; bounded history handles
    // loops without suppressing the next click or swipe.
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
        mainbar_programmatic_navigation = true;
        lv_tileview_set_tile_act( mainbar, tile_pos_table[ tile_number ].x, tile_pos_table[ tile_number ].y, anim );
        mainbar_last_active_tile = tile_number;
        mainbar_programmatic_navigation = false;
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
    // Menu groups are horizontal, including multi-page application menus.
    if (y_delta != 0) return false;
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
    if (y == 0) mainbar_clear_history();
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
            for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) lv_indev_wait_release(input);
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
            for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) lv_indev_wait_release(input);
            return( true );
        }

        if ( abs_y >= swipe_threshold && abs_y > abs_x ) {
            mainbar_tdeck_pro_jump_relative( 0, dy < 0 ? 1 : -1 );
            lv_indev_reset( NULL, NULL );
            for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) lv_indev_wait_release(input);
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
