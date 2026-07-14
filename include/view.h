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

#ifndef VIEW_H
#define VIEW_H

#include <stdbool.h>

typedef struct view_info {
    uint16_t backbuffer_offset;
    uint8_t view_index;
    uint8_t loop_index;
    uint16_t loop_offset;
    uint8_t cel_index;
    uint8_t number_of_cels;
    uint8_t number_of_loops;
    uint8_t width;
    uint8_t height;
    int16_t x_pos;
    int16_t y_pos;
    bool priority_override;
    uint8_t priority;
    bool priority_set;
    uint16_t desc_offset;
} view_info_t;

bool draw_cel(view_info_t *info, uint8_t cel);
void erase_view(view_info_t *info);
bool select_loop(view_info_t *info, uint8_t loop_num);
uint8_t get_num_loops(uint8_t view_number);
void view_set(view_info_t *info, uint8_t view_num);
bool view_load(uint8_t view_num);
void view_unload(uint8_t view_num);
void view_purge(uint16_t freed_offset);
void view_init(void);

#endif