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

#ifndef SOUND_H
#define SOUND_H

#include "main.h"
extern volatile uint8_t sound_flag_end;
extern volatile bool sound_flag_needs_set;

void sound_init_opl(void);

void sound_play(uint8_t sound_num, uint8_t flag_at_end);
void sound_stop(void);
void sound_interrupt_handler(void);

#endif