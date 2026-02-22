/***************************************************************************
    MEGA65-AGI -- Sierra AGI interpreter for the MEGA65
    Copyright (C) 2025  Keith Henrickson

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
***************************************************************************/

#ifndef DIALOG_H
#define DIALOG_H

#include "main.h"
typedef struct menu_entry {
    uint8_t text[25];
    uint8_t enabled;
    uint8_t controller;
    uint8_t end;
} menu_entry_t;

extern uint8_t __far menu_opts_used;
extern menu_entry_t __far menu_entries[40];

void dialog_draw_itemlist(void);
void dialog_draw_menubar(bool mouse_trigger);
void dialog_handle_setkey(uint8_t ascii, uint8_t keycode, uint8_t controller);
void dialog_recall(void);
void dialog_gamesave_handler(char *filename);
void dialog_gamesave_begin(bool save);
void dialog_clear_keyboard(void);
void dialog_enable_menu_item(uint8_t controller_number);
void dialog_enable_all_menu_items(void);
void dialog_disable_menu_item(uint8_t controller_number);
void dialog_set_menu(uint8_t message_number);
void dialog_set_menu_item(uint8_t message_number, uint8_t controller);
void dialog_get_string(uint8_t destination_str, uint8_t prompt, uint8_t row, uint8_t column, uint8_t max);
bool dialog_show_valist(bool accept_input, bool ok_cancel, bool draw_only, uint8_t __far *message_string, va_list ap);
bool dialog_show_enginehigh(bool accept_input, bool ok_cancel, bool draw_only, uint8_t __far *message_string, ...);
bool dialog_handleinput(bool force_accept, bool mapkeys, bool *cancelled);
void dialog_close(void);
bool dialog_proc(void);
void dialog_init(void);

#endif
