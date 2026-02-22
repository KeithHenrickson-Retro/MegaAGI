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

#ifndef PARSER_H
#define PARSER_H

extern uint32_t token_data_offset;
extern uint8_t parser_word_index;
extern uint16_t parser_word_numbers[20];
extern const char * parser_word_pointers[20];

bool parser_check_wov_internal(uint8_t *out);
bool parser_decode_string(char *target);
void parser_init(void);

#endif
