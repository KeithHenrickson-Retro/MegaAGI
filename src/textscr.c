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

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <calypsi/intrinsics6502.h>
#include <mega65.h>

#include "textscr.h"
#include "engine.h"
#include "gfx.h"
#include "logic.h"
#include "main.h"
#include "mapper.h"
#include "memmanage.h" 
#include "parser.h"

static void textscr_print_asciistr(uint8_t x, uint8_t y, uint8_t __far *output);

#pragma clang section bss="extradata"
__far uint8_t formatted_string_buffer[1024];
__far uint8_t print_string_buffer[1024];

#pragma clang section bss="banked_bss" data="eh_data" rodata="eh_rodata" text="eh_text"
uint8_t my_ultoa_invert(unsigned long val, char *str, int base)
{
  uint8_t len = 0;
  do
    {
      int v;

      v   = val % base;
      val = val / base;

      if (v <= 9)
        {
          v += '0';
        }
      else
        {
          v += 'A' - 10;
        }

      *str++ = v;
      len++;
    }
  while (val);

  return len;
}

uint8_t my_atoi(uint8_t __far *str, uint8_t __far **endptr)
{
  uint8_t val = 0;
  uint8_t digit;
  while (*str != 0) {
    if (*str >= '0' && *str <= '9') {
      digit = *str - '0';
    } else {
      break;
    }
    val = (val * 10) + digit;
    str++;
  }
  if (endptr) {
    *endptr = str;
  }
  return val;
}

uint16_t textscr_format_string_valist(uint8_t __far *formatstring, va_list ap) {
  char buffer[17];
  uint16_t padlen = 0;
  uint8_t __far *ascii_string = formatstring;
  while (*ascii_string != 0) {
    uint8_t asciichar = *ascii_string;
    if (asciichar == '%') {
      ascii_string++;
      switch (*ascii_string) {
        case 'p': {
          uint8_t padcol = my_atoi(ascii_string + 1, &ascii_string);
          while (padlen < padcol) {
            formatted_string_buffer[padlen] = ' ';
            padlen++;
          }
          break;
        }
        case 'w': {
          uint8_t wordnum = my_atoi(ascii_string + 1, &ascii_string);
          const char *wordptr = parser_word_pointers[wordnum - 1];
          uint8_t wordchr = (uint8_t)*wordptr;
          while (wordchr != 0) {
            formatted_string_buffer[padlen] = wordchr;
            padlen++;
            wordptr++;
            wordchr = (uint8_t)*wordptr;
          };
          break;
        }
        case 'm': {
          uint8_t string_number = my_atoi(ascii_string + 1, &ascii_string);
          uint8_t __far *src_string = logic_locate_message(255, string_number);
          uint8_t string_char = *src_string;
          while (string_char != 0) {
            formatted_string_buffer[padlen] = string_char;
            padlen++;
            src_string++;
            string_char = *src_string;
          };
          break;
        }
        case 'M': {
          uint32_t string_number = va_arg(ap, unsigned int);
          uint8_t __far *src_string = logic_locate_message(255, string_number);
          uint8_t string_char = *src_string;
          while (string_char != 0) {
            formatted_string_buffer[padlen] = string_char;
            padlen++;
            src_string++;
            string_char = *src_string;
          };
          ascii_string++;
          break;
        }
        case 's': {
          uint8_t string_number = my_atoi(ascii_string + 1, &ascii_string);
          uint8_t __far *src_string = global_strings + (40 * string_number);
          uint8_t string_char = *src_string;
          while (string_char != 0) {
            formatted_string_buffer[padlen] = string_char;
            padlen++;
            src_string++;
            string_char = *src_string;
          };
          break;
        }
        case 'S': {
          const __far char *wordptr = va_arg(ap, char __far *);
          uint8_t wordchr = (uint8_t)*wordptr;
          while (wordchr != 0) {
            formatted_string_buffer[padlen] = wordchr;
            padlen++;
            wordptr++;
            wordchr = (uint8_t)*wordptr;
          };
          ascii_string++;
          break;
        }
        case 'H': {
          const __huge char *wordptr = va_arg(ap, char __huge *);
          uint8_t wordchr = (uint8_t)*wordptr;
          while (wordchr != 0) {
            formatted_string_buffer[padlen] = wordchr;
            padlen++;
            wordptr++;
            wordchr = (uint8_t)*wordptr;
          };
          ascii_string++;
          break;
        }
        case 'd':
        case 'x': {
          uint32_t param = va_arg(ap, unsigned int);
          uint8_t len = my_ultoa_invert(param, (char *)buffer, (*ascii_string == 'x') ? 16 : 10);
          for (; len > 0; len--) {
            formatted_string_buffer[padlen] = buffer[len-1];
            padlen++;
          }
          ascii_string++;
          break;
        }
        case 'D':
        case 'X': {
          uint32_t param = va_arg(ap, unsigned long);
          uint8_t len = my_ultoa_invert(param, (char *)buffer, (*ascii_string == 'X') ? 16 : 10);
          for (; len > 0; len--) {
            formatted_string_buffer[padlen] = buffer[len-1];
            padlen++;
          }
          ascii_string++;
          break;
        }
      }
      continue;
    } else if (*ascii_string == '\\') {
        ascii_string++;
        if (*ascii_string == 'n') {
            asciichar = '\n';
        } else {
            asciichar = *ascii_string;
        }
    }
    formatted_string_buffer[padlen] = asciichar;
    padlen++;
    ascii_string++;
  }
  formatted_string_buffer[padlen] = 0;
  return padlen;
}

uint16_t textscr_print_ascii_himem(uint8_t x, uint8_t y, uint8_t *formatstring, ...) {
    va_list ap;
    va_start(ap, formatstring);

    memmanage_strcpy_near_far(print_string_buffer, formatstring);
    uint16_t len = textscr_format_string_valist(print_string_buffer, ap);
    va_end(ap);

    textscr_print_asciistr(x, y, formatted_string_buffer);
    return len;
}

#pragma clang section bss="banked_bss" data="enginedata" rodata="enginerodata" text="enginetext"

static uint16_t printpos_x = 0;
static uint16_t printpos_y = 0;
static uint16_t forecolor;
static uint16_t backcolor;

static  void textscr_print_asciistr(uint8_t x, uint8_t y, uint8_t __far *output);

static const unsigned char ascii_to_c64_screen[128] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

void textscr_set_color(uint8_t foreground, uint8_t background) {
  forecolor = foreground << 8;
  backcolor = background << 8;
}

void textscr_print_scncode(uint8_t scncode) {
  screen_memory_0[printpos_y].backtiles_chars[printpos_x] = 0x00a0;
  screen_memory_1[printpos_y].backtiles_chars[printpos_x] = 0x00a0;
  color_memory[printpos_y].backtiles_chars[printpos_x] = backcolor;
  screen_memory_0[printpos_y].foretiles_chars[printpos_x] = scncode;
  screen_memory_1[printpos_y].foretiles_chars[printpos_x] = scncode;
  color_memory[printpos_y].foretiles_chars[printpos_x] = forecolor;
  printpos_x++;
}

void textscr_print_asciichar(uint8_t character) {
  character = ascii_to_c64_screen[character];
  textscr_print_scncode(character);
}

void textscr_set_printpos(uint8_t x, uint8_t y) {
  printpos_x = x;
  printpos_y = y; 
}

void textscr_print_asciistr(uint8_t x, uint8_t y, uint8_t __far *output) {
  textscr_set_printpos(x,y);

  uint8_t __far *ascii_string = output;
  while (*ascii_string != 0) {
    textscr_print_asciichar(*ascii_string);
    ascii_string++;
  }
}

void textscr_clear_line(uint8_t y) {
  if ((y > 0) && (y <= 21)) {
    for (int x = 0; x < 40; x++) {
      screen_memory_0[y].backtiles_chars[x] = 0x0020;        
      screen_memory_1[y].backtiles_chars[x] = 0x0020;
      screen_memory_0[y].foretiles_chars[x] = 0x0020;        
      screen_memory_1[y].foretiles_chars[x] = 0x0020;
      color_memory[y].foretiles_chars[x] = 0x0100;       
      color_memory[y].backtiles_chars[x] = 0x0000;       
    }
  } else {
    for (int x = 0; x < 40; x++) {
      color_memory[y].backtiles_chars[x] = 0x0000;       
      screen_memory_0[y].backtiles_chars[x] = 0x00a0;        
      screen_memory_1[y].backtiles_chars[x] = 0x00a0;
      screen_memory_0[y].foretiles_chars[x] = 0x0020;        
      screen_memory_1[y].foretiles_chars[x] = 0x0020;
      color_memory[y].foretiles_chars[x] = 0x0100;
    }
  }
}

void textscr_set_textmode(bool enable_text) {
  if (enable_text && !game_text) {
    textscr_set_color(COLOR_WHITE, COLOR_BLACK);
    for (uint8_t i = 0; i < 25; i++) {
      textscr_print_ascii(0, i, (uint8_t *)"%p40");
    }
    game_text = true;
  } else if (!enable_text && game_text) {
    game_text = false;
    for (uint8_t i = 0; i < 25; i++) {
      textscr_clear_line(i);
    }
    engine_update_status_line(true);
    engine_clear_keyboard();
  }
}

uint16_t textscr_print_ascii(uint8_t x, uint8_t y, uint8_t *formatstring, ...) {
    va_list ap;
    va_start(ap, formatstring);

    memmanage_strcpy_near_far(print_string_buffer, formatstring);
    select_engine_enginehigh_mem();
    uint16_t len = textscr_format_string_valist(print_string_buffer, ap);
    select_previous_bank();
    va_end(ap);

    textscr_print_asciistr(x, y, formatted_string_buffer);
    return len;
}

void textscr_init(void) {
  
}