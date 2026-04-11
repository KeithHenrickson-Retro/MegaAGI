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

#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>
extern uint8_t __far *logic_vars;
extern uint8_t __far *logic_flags;
extern uint32_t object_data_offset;
extern bool debug;
typedef struct logic_info {
    uint16_t offset;
    uint8_t __far *text_offset;
    uint16_t scan_start;
} logic_info_t;

uint8_t __far * logic_locate_message(uint8_t logic_num_query, uint8_t message_num);
void logic_set_flag(uint8_t flag);
void logic_reset_flag(uint8_t flag);
void logic_toggle_flag(uint8_t flag);
bool logic_flag_isset(uint8_t flag);
void logic_set_controller(uint8_t flag);
void logic_reset_all_controllers(void);
void logic_load(uint8_t logic_num);
void logic_run(void);
void logic_purge(uint16_t freed_offset);
void logic_init(void);

#endif
