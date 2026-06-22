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

#ifndef MAIN_H
#define MAIN_H

#include "stdbool.h"
#include "gamesave.h"

struct __F018 {
  uint8_t dmalowtrig;
  uint8_t dmahigh;
  uint8_t dmabank;
  uint8_t enable018;
  uint8_t addrmb;
  uint8_t etrig;
};

#define CHAR_CLEARHOME '\223'
#define MODKEY (*(volatile uint8_t *)0xd60a)
#define ASCIIKEY (*(volatile uint8_t *)0xd610)
#define PETSCIIKEY (*(volatile uint8_t *)0xd619)
#define VICREGS ((volatile uint8_t *)0xd000)
#define POKE(X, Y) (*(volatile uint8_t*)(X)) = Y
#define PEEK(X) (*(volatile uint8_t*)(X))

/// DMA controller
#define DMA (*(volatile struct __F018 *)0xd700)

extern uint8_t __far *global_strings;
extern volatile bool run_engine;
extern volatile bool game_timeslot_ready;

#endif