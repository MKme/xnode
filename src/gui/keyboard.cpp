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
#include "keyboard.h"
#include "statusbar.h"
#include "widget_factory.h"

#include <string.h>

LV_FONT_DECLARE(lv_font_montserrat_22);

typedef enum {
    KB_PAGE_AM,
    KB_PAGE_NZ,
    KB_PAGE_CAP_AM,
    KB_PAGE_CAP_NZ,
    KB_PAGE_SYMBOL
} keyboard_page_t;

static lv_obj_t *kb_screen = NULL;
static lv_obj_t *kb_textarea = NULL;
static lv_obj_t *kb = NULL;
static lv_obj_t *nkb = NULL;
static lv_obj_t *kb_user_textarea = NULL;
static bool kb_style_initialized = false;
static lv_style_t kb_textarea_style;
static lv_style_t kb_button_style;
static keyboard_page_t kb_page = KB_PAGE_AM;

#define KB_CTRL_KEY ( LV_BTNMATRIX_CTRL_NO_REPEAT | 1 )

static const char * const kb_map_am[] = {
    "a", "b", "c", "d", "\n",
    "e", "f", "g", "h", "\n",
    "i", "j", "k", "l", "\n",
    "m", "N-Z", "123", LV_SYMBOL_BACKSPACE, "\n",
    LV_SYMBOL_CLOSE, "CAP", "space", LV_SYMBOL_OK, ""
};

static const char * const kb_map_nz[] = {
    "n", "o", "p", "q", "\n",
    "r", "s", "t", "u", "\n",
    "v", "w", "x", "y", "\n",
    "z", "A-M", "123", LV_SYMBOL_BACKSPACE, "\n",
    LV_SYMBOL_CLOSE, "CAP", "space", LV_SYMBOL_OK, ""
};

static const char * const kb_map_cap_am[] = {
    "A", "B", "C", "D", "\n",
    "E", "F", "G", "H", "\n",
    "I", "J", "K", "L", "\n",
    "M", "N-Z", "123", LV_SYMBOL_BACKSPACE, "\n",
    LV_SYMBOL_CLOSE, "low", "space", LV_SYMBOL_OK, ""
};

static const char * const kb_map_cap_nz[] = {
    "N", "O", "P", "Q", "\n",
    "R", "S", "T", "U", "\n",
    "V", "W", "X", "Y", "\n",
    "Z", "A-M", "123", LV_SYMBOL_BACKSPACE, "\n",
    LV_SYMBOL_CLOSE, "low", "space", LV_SYMBOL_OK, ""
};

static const char * const kb_map_symbol[] = {
    "1", "2", "3", "4", "\n",
    "5", "6", "7", "8", "\n",
    "9", "0", ".", ",", "\n",
    "?", "!", "-", LV_SYMBOL_BACKSPACE, "\n",
    LV_SYMBOL_CLOSE, "A-M", "space", LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t kb_ctrl_alpha[] = {
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY
};

static const lv_btnmatrix_ctrl_t kb_ctrl_symbol[] = {
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY,
    KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY, KB_CTRL_KEY
};

static void kb_event_cb(lv_obj_t * ta, lv_event_t event);
static void keyboard_set_page( keyboard_page_t page );
static bool keyboard_handle_text_key( lv_obj_t * keyboard, const char * txt );
static void keyboard_layout_text( void );

void keyboard_prelim( void ) {
    if( !kb_style_initialized ) {
        lv_style_copy( &kb_textarea_style, ws_get_button_style() );
        lv_style_set_text_font( &kb_textarea_style, LV_STATE_DEFAULT, &lv_font_montserrat_22 );
        lv_style_set_pad_top( &kb_textarea_style, LV_STATE_DEFAULT, 3 );
        lv_style_set_pad_bottom( &kb_textarea_style, LV_STATE_DEFAULT, 3 );
        lv_style_set_pad_left( &kb_textarea_style, LV_STATE_DEFAULT, 5 );
        lv_style_set_pad_right( &kb_textarea_style, LV_STATE_DEFAULT, 5 );

        lv_style_copy( &kb_button_style, ws_get_button_style() );
        lv_style_set_text_font( &kb_button_style, LV_STATE_DEFAULT, &lv_font_montserrat_22 );
        lv_style_set_pad_inner( &kb_button_style, LV_STATE_DEFAULT, 0 );
        lv_style_set_pad_top( &kb_button_style, LV_STATE_DEFAULT, 1 );
        lv_style_set_pad_bottom( &kb_button_style, LV_STATE_DEFAULT, 1 );
        lv_style_set_pad_left( &kb_button_style, LV_STATE_DEFAULT, 1 );
        lv_style_set_pad_right( &kb_button_style, LV_STATE_DEFAULT, 1 );

        kb_screen = lv_cont_create( lv_scr_act(), NULL );
        lv_obj_add_style( kb_screen, LV_OBJ_PART_MAIN, SETUP_STYLE );
        lv_obj_set_size( kb_screen, lv_disp_get_hor_res( NULL ) , lv_disp_get_ver_res( NULL ) );
        lv_obj_align( kb_screen, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 0 );
        
        kb_textarea = lv_textarea_create( kb_screen, NULL );
        lv_obj_add_protect( kb_textarea, LV_PROTECT_CLICK_FOCUS );
        lv_obj_add_style( kb_textarea, LV_TEXTAREA_PART_BG, &kb_textarea_style );
        lv_obj_set_size( kb_textarea, lv_disp_get_hor_res( NULL ) - ( THEME_PADDING * 2 ), 54 );
        lv_textarea_set_one_line( kb_textarea, false);
        lv_textarea_set_cursor_hidden( kb_textarea, false );
        lv_obj_align( kb_textarea, kb_screen, LV_ALIGN_IN_TOP_MID, 0, 2 );
        kb_style_initialized = true;
    }
}


void keyboard_setup( void ) {
    /*
     * check if keyboard already initialized
     */
    if ( kb != NULL )
        return;

    keyboard_prelim();

    kb = lv_keyboard_create( kb_screen , NULL);
    keyboard_set_page( KB_PAGE_AM );
    lv_obj_set_size( kb, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) - 58 );
    lv_obj_align( kb, kb_screen, LV_ALIGN_IN_BOTTOM_MID, 0, 0 );
    lv_obj_add_style( kb, LV_OBJ_PART_ALL, SETUP_STYLE );
    lv_obj_add_style( kb, LV_KEYBOARD_PART_BTN, &kb_button_style );
    lv_keyboard_set_cursor_manage( kb, true);
    lv_obj_set_event_cb( kb, kb_event_cb );

    keyboard_hide();
}

void num_keyboard_setup( void ) {
    /*
     * check if keyboard already initialized
     */
    if ( nkb != NULL )
        return;

    keyboard_prelim();
    nkb = lv_keyboard_create( kb_screen , NULL);
    lv_obj_set_size( nkb, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) - 58 );
    lv_obj_align( nkb, kb_screen, LV_ALIGN_IN_BOTTOM_MID, 0, 0 );
    lv_obj_add_style( nkb, LV_OBJ_PART_ALL, SETUP_STYLE );
    lv_obj_add_style( nkb, LV_KEYBOARD_PART_BTN, &kb_button_style );
    lv_keyboard_set_mode( nkb, LV_KEYBOARD_MODE_NUM);
    lv_keyboard_set_cursor_manage( nkb, true);
    lv_obj_set_event_cb( nkb, kb_event_cb );

    keyboard_hide();
}

static void kb_event_cb( lv_obj_t * ta, lv_event_t event ) {

    if ( event == LV_EVENT_VALUE_CHANGED ) {
        const uint32_t *event_btn = (const uint32_t*)lv_event_get_data();
        uint16_t btn_id = event_btn != NULL ? (uint16_t)( *event_btn ) : lv_btnmatrix_get_active_btn( ta );
        if ( btn_id != LV_BTNMATRIX_BTN_NONE ) {
            const char * txt = lv_btnmatrix_get_btn_text( ta, btn_id );
            if ( txt != NULL && keyboard_handle_text_key( ta, txt ) ) {
                return;
            }
        }
    }

    lv_keyboard_def_event_cb( ta, event );
    switch( event ) {
        case( LV_EVENT_CANCEL ):    keyboard_hide();
                                    break;
        case( LV_EVENT_APPLY ):     if ( kb_user_textarea != NULL ) {
                                        lv_textarea_set_text( kb_user_textarea, lv_textarea_get_text( kb_textarea ) );
                                    }
                                    keyboard_hide();
                                    break;
    }
}

void keyboard_set_textarea( lv_obj_t *textarea ){
    /*
     * check if keyboard already initialized
     */
    if ( kb == NULL )
        return;

    kb_user_textarea = textarea;
    lv_textarea_set_text( kb_textarea, lv_textarea_get_text( textarea ) );
    lv_keyboard_set_textarea( kb, kb_textarea );
    keyboard_set_page( KB_PAGE_AM );
    keyboard_show();
}

void num_keyboard_set_textarea( lv_obj_t *textarea ){
    /*
     * check if keyboard already initialized
     */
    if ( nkb == NULL )
        return;

    kb_user_textarea = textarea;
    lv_textarea_set_text( kb_textarea, lv_textarea_get_text( textarea ) );
    lv_keyboard_set_textarea( nkb, kb_textarea );
    num_keyboard_show();
}

void keyboard_hide( void ) {
    if ( kb_screen != NULL ) {
    	lv_obj_set_hidden( kb_screen, true );
    }

    if ( kb_textarea != NULL) {
    	lv_obj_set_hidden( kb_textarea, true );
    }

    if( kb != NULL ) {
    	lv_obj_set_hidden( kb, true );
    }

    if( nkb != NULL ) {
    	lv_obj_set_hidden( nkb, true );
    }
}

void keyboard_show( void ) {
    /*
     * check if keyboard already initialized
     */
    if ( kb == NULL )
        return;

    lv_obj_set_hidden( kb_screen, false );
    lv_obj_set_hidden( kb_textarea, false );
    lv_obj_set_hidden( kb, false );
    if ( nkb != NULL ) {
        lv_obj_set_hidden( nkb, true );
    }
    keyboard_layout_text();
    lv_obj_move_foreground( kb_screen );

}

void num_keyboard_show( void ) {
    /*
     * check if keyboard already initialized
     */
    if ( nkb == NULL )
        return;
    lv_obj_set_hidden( kb_screen, false );
    lv_obj_set_hidden( kb_textarea, false );
    if ( kb != NULL ) {
        lv_obj_set_hidden( kb, true );
    }
    lv_obj_set_hidden( nkb, false );
    keyboard_layout_text();
    lv_obj_align( nkb, kb_screen, LV_ALIGN_IN_BOTTOM_MID, 0, 0 );
    lv_obj_move_foreground( kb_screen );
 }

static void keyboard_layout_text( void ) {
    lv_coord_t screen_w = lv_disp_get_hor_res( NULL );
    lv_coord_t screen_h = lv_disp_get_ver_res( NULL );

    lv_obj_set_size( kb_screen, screen_w, screen_h );
    lv_obj_align( kb_screen, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 0 );

    const lv_coord_t inset = THEME_PADDING;
    const lv_coord_t keyboard_w = screen_w - ( inset * 2 );

    lv_obj_set_size( kb_textarea, keyboard_w, 54 );
    lv_obj_align( kb_textarea, kb_screen, LV_ALIGN_IN_TOP_MID, 0, 2 );

    if ( kb != NULL ) {
        lv_obj_set_size( kb, keyboard_w, screen_h - 58 );
        lv_obj_align( kb, kb_screen, LV_ALIGN_IN_BOTTOM_MID, 0, 0 );
    }
    if ( nkb != NULL ) {
        lv_obj_set_size( nkb, keyboard_w, screen_h - 58 );
        lv_obj_align( nkb, kb_screen, LV_ALIGN_IN_BOTTOM_MID, 0, 0 );
    }
}

static void keyboard_set_page( keyboard_page_t page ) {
    if ( kb == NULL ) {
        kb_page = page;
        return;
    }

    kb_page = page;
    switch ( page ) {
        case KB_PAGE_AM:
            lv_keyboard_set_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, (const char **)kb_map_am );
            lv_keyboard_set_ctrl_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_ctrl_alpha );
            lv_keyboard_set_mode( kb, LV_KEYBOARD_MODE_TEXT_LOWER );
            break;
        case KB_PAGE_NZ:
            lv_keyboard_set_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, (const char **)kb_map_nz );
            lv_keyboard_set_ctrl_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_ctrl_alpha );
            lv_keyboard_set_mode( kb, LV_KEYBOARD_MODE_TEXT_LOWER );
            break;
        case KB_PAGE_CAP_AM:
            lv_keyboard_set_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, (const char **)kb_map_cap_am );
            lv_keyboard_set_ctrl_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_ctrl_alpha );
            lv_keyboard_set_mode( kb, LV_KEYBOARD_MODE_TEXT_LOWER );
            break;
        case KB_PAGE_CAP_NZ:
            lv_keyboard_set_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, (const char **)kb_map_cap_nz );
            lv_keyboard_set_ctrl_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_ctrl_alpha );
            lv_keyboard_set_mode( kb, LV_KEYBOARD_MODE_TEXT_LOWER );
            break;
        case KB_PAGE_SYMBOL:
            lv_keyboard_set_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, (const char **)kb_map_symbol );
            lv_keyboard_set_ctrl_map( kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_ctrl_symbol );
            lv_keyboard_set_mode( kb, LV_KEYBOARD_MODE_TEXT_LOWER );
            break;
    }
    lv_obj_invalidate( kb );
}

static bool keyboard_handle_text_key( lv_obj_t * keyboard, const char * txt ) {
    if ( keyboard != kb ) {
        return( false );
    }

    if ( strcmp( txt, "A-M" ) == 0 ) {
        keyboard_set_page( KB_PAGE_AM );
        return( true );
    }
    if ( strcmp( txt, "N-Z" ) == 0 ) {
        keyboard_set_page( kb_page == KB_PAGE_CAP_AM ? KB_PAGE_CAP_NZ : KB_PAGE_NZ );
        return( true );
    }
    if ( strcmp( txt, "CAP" ) == 0 ) {
        keyboard_set_page( kb_page == KB_PAGE_NZ ? KB_PAGE_CAP_NZ : KB_PAGE_CAP_AM );
        return( true );
    }
    if ( strcmp( txt, "low" ) == 0 ) {
        keyboard_set_page( kb_page == KB_PAGE_CAP_NZ ? KB_PAGE_NZ : KB_PAGE_AM );
        return( true );
    }
    if ( strcmp( txt, "123" ) == 0 ) {
        keyboard_set_page( KB_PAGE_SYMBOL );
        return( true );
    }

    lv_obj_t * target = lv_keyboard_get_textarea( keyboard );
    if ( target == NULL ) {
        return( true );
    }

    if ( strcmp( txt, "space" ) == 0 || strcmp( txt, "SPC" ) == 0 ) {
        lv_textarea_add_char( target, ' ' );
        return( true );
    }
    if ( strcmp( txt, LV_SYMBOL_BACKSPACE ) == 0 ) {
        lv_textarea_del_char( target );
        return( true );
    }
    if ( strcmp( txt, LV_SYMBOL_CLOSE ) == 0 ) {
        keyboard_hide();
        return( true );
    }
    if ( strcmp( txt, LV_SYMBOL_OK ) == 0 ) {
        if ( kb_user_textarea != NULL ) {
            lv_textarea_set_text( kb_user_textarea, lv_textarea_get_text( target ) );
        }
        keyboard_hide();
        return( true );
    }

    lv_textarea_add_text( target, txt );
    return( true );
}
