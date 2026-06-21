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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <calypsi/intrinsics6502.h>
#include <mega65.h>
#include <math.h>

#include "dialog.h"
#include "engine.h"
#include "gfx.h"
#include "volume.h"
#include "pic.h"
#include "textscr.h"
#include "main.h"
#include "memmanage.h"
#include "mapper.h"

#define Q15_16_INT_TO_Q(QVAR, WHOLE) \
  QVAR.part.whole = WHOLE;\
  QVAR.part.fractional = 0;

typedef struct fill_info {
    int16_t x1;
    int16_t x2;
    int16_t y;
    int16_t dy;
} fill_info_t;

typedef union q15_16 {
  int32_t full_value;
  struct part_tag {
    uint16_t fractional;
    int16_t whole;
  } part;
} q15_16t;

#pragma clang section bss="extradata"
__far static fill_info_t fills[128];
__far pic_descriptor_t pic_descriptors[256];
#pragma clang section bss=""

#pragma clang section bss="banked_bss" data="picdraw_data" rodata="picdraw_rodata" text="picdraw_text"
static    uint8_t pic_color;
static    uint8_t priority_color;
static    uint8_t pic_on;
static    uint8_t priority_on;
static uint8_t last_relative_x;
static uint8_t last_relative_y;
static uint16_t fill_pointer;

void pset(uint8_t x, uint8_t y) {
    if (pic_on) {
        gfx_plotput(x, y, pic_color);
    }
    if (priority_on) {
        gfx_plotput(x, y, priority_color | 0x80);
    }
}

int agi_q15round(q15_16t aNumber, int16_t dirn)
{
  int16_t floornum = aNumber.part.whole;
  int16_t ceilnum = aNumber.part.whole+1;

   if (dirn < 0)
      return ((aNumber.part.fractional <= 0x8042) ?
        floornum : ceilnum);
   return ((aNumber.part.fractional < 0x7fbe) ?
        floornum : ceilnum);
}

void pic_drawslowline(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour) {
   int16_t height, width;
   q15_16t x, y, dependent;
   int8_t increment;

   height = ((int16_t)y2 - y1);
   width = ((int16_t)x2 - x1);
   uint8_t absheight = abs(height);
   uint8_t abswidth = abs(width);
   if (abs(width) > abs(height)) {
      Q15_16_INT_TO_Q(x, x1);
      Q15_16_INT_TO_Q(y, y1);
      if (width > 0) {
        increment = 1;
      } else {
        increment = -1;
      }
      Q15_16_INT_TO_Q(dependent, height);
      dependent.full_value = (width  == 0 ? 0:(dependent.full_value/abswidth));
      for (; x.part.whole != x2; x.part.whole += increment) {
        int roundx = agi_q15round(x, increment);
        int roundy = agi_q15round(y, dependent.part.whole);
         gfx_plotput(roundx, roundy, colour);
         y.full_value += dependent.full_value;
      }
      gfx_plotput(x2, y2, colour);
   }
   else {
      Q15_16_INT_TO_Q(x, x1);
      Q15_16_INT_TO_Q(y, y1);
      if (height > 0) {
        increment = 1;
      } else {
        increment = -1;
      }
      Q15_16_INT_TO_Q(dependent, width);
      dependent.full_value = (height == 0 ? 0:(dependent.full_value/absheight));
      for (; y.part.whole!=y2; y.part.whole += increment) {
        uint8_t roundx = agi_q15round(x, dependent.part.whole);
        uint8_t roundy = agi_q15round(y, increment);
         gfx_plotput(roundx, roundy, colour);
         x.full_value += dependent.full_value;
      }
      gfx_plotput(x2,y2, colour);
   }
}

void draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    if (pic_on) {
        pic_drawslowline(x1, y1, x2, y2, pic_color);
    }
    if (priority_on) {
        pic_drawslowline(x1, y1, x2, y2, priority_color | 0x80);
    }
}

void draw_relative_line(uint8_t coord) {
    int8_t rel_x = coord >> 4;
    int8_t rel_y = coord & 0x0f;
    if (rel_x & 0x08) {
        rel_x = -1 * (rel_x & 0x07);
    }
    if (rel_y & 0x08) {
        rel_y = -1 * (rel_y & 0x07);
    }
    draw_line(last_relative_x, last_relative_y, last_relative_x + rel_x, last_relative_y + rel_y);
    last_relative_x = last_relative_x + rel_x;
    last_relative_y = last_relative_y + rel_y;
}

uint8_t can_fill(uint8_t x, uint8_t y) {
    if ((x >= 160) || (y >= 168)) {
        return 0;
    }
    if (!priority_on && pic_on && (pic_color != 15)) {
        return (gfx_get(x, y) == 15);
    }
    if (priority_on && !pic_on && (priority_color != 4)) {
        return (gfx_getprio(x, y) == 4);
    }
    return (pic_on && (gfx_get(x, y) == 15) && (pic_color != 15));
}

void draw_fill(uint8_t in_x, uint8_t in_y) {
    if (!can_fill(in_x, in_y)) {
        return;
    }
    fill_pointer = 2;
    fills[1] = (fill_info_t){.x1 = in_x, .x2 = in_x, .y = in_y, .dy = 1};
    fills[2] = (fill_info_t){.x1 = in_x, .x2 = in_x, .y = in_y - 1, .dy = -1};
    while (fill_pointer > 0) {
        fill_info_t cur_fill = fills[fill_pointer];
        fill_pointer--;

        int16_t loc_x = cur_fill.x1;
        if (can_fill(loc_x, cur_fill.y)) {
            int16_t start_x = loc_x - 1;
            while (can_fill(loc_x - 1, cur_fill.y)) {
                loc_x = loc_x - 1;
            }
            if (loc_x <= start_x) {
                draw_line(start_x, cur_fill.y, loc_x, cur_fill.y);
            }
            if (loc_x < cur_fill.x1) {
                fill_pointer++;
                fills[fill_pointer] = (fill_info_t){.x1 = loc_x, .x2 = cur_fill.x1 - 1, .y = cur_fill.y - cur_fill.dy, .dy = -cur_fill.dy};
            }
        }

        while(cur_fill.x1 <= cur_fill.x2) {
            int16_t start_x = cur_fill.x1;
            while (can_fill(cur_fill.x1, cur_fill.y)) {
                cur_fill.x1 = cur_fill.x1 + 1;
            }
            if (cur_fill.x1 > start_x) {
                draw_line(start_x, cur_fill.y, cur_fill.x1 - 1, cur_fill.y);
            }
            if (cur_fill.x1 > loc_x) {
                fill_pointer++;
                fills[fill_pointer] = (fill_info_t){.x1 = loc_x, .x2 = cur_fill.x1 - 1, .y = cur_fill.y + cur_fill.dy, .dy = cur_fill.dy};
            }
            if ((cur_fill.x1 - 1) > cur_fill.x2) {
                fill_pointer++;
                fills[fill_pointer] = (fill_info_t){.x1 = cur_fill.x2 + 1, .x2 = cur_fill.x1 - 1, .y = cur_fill.y - cur_fill.dy, .dy = -cur_fill.dy};
            }
            cur_fill.x1 = cur_fill.x1 + 1;
            while ((cur_fill.x1 < cur_fill.x2) && (!can_fill(cur_fill.x1, cur_fill.y))) {
                cur_fill.x1 = cur_fill.x1 + 1;
            }
            loc_x = cur_fill.x1;
        }
    }
}

void draw_pic(uint8_t pic_num, bool clear_screen) {
    if (clear_screen) {
        gfx_cleargfx(true);
    }
    dialog_close();
    uint8_t __far *pic_file;
    pic_file = chipmem_base + pic_descriptors[pic_num].offset;
    pic_on = 0;
    priority_on = 0;
    uint16_t index = 0;
    do {
        uint8_t command = pic_file[index];
        index++;
        switch(command) {
            case 0xF0:
                pic_color = pic_file[index];
                index++;
                pic_on = 1;
                break;
            case 0xF1:
                pic_on = 0;
                break;
            case 0xF2:
                priority_color = pic_file[index];
                index++;
                priority_on = 1;
                break;
            case 0xF3:
                priority_on = 0;
                break;
            case 0xF4:
                last_relative_x = pic_file[index];
                last_relative_y = pic_file[index+1];
                index += 2;
                while ((pic_file[index] & 0xf0) != 0xf0) {
                    draw_line(last_relative_x, last_relative_y, last_relative_x, pic_file[index]);
                    last_relative_y = pic_file[index];
                    index += 1;
                    if ((pic_file[index] & 0xf0) == 0xf0) {
                        break;
                    }
                    draw_line(last_relative_x, last_relative_y, pic_file[index], last_relative_y);
                    last_relative_x = pic_file[index];
                    index += 1;
                }
                break;
            case 0xF5:
                last_relative_x = pic_file[index];
                last_relative_y = pic_file[index+1];
                index += 2;
                while ((pic_file[index] & 0xf0) != 0xf0) {
                    draw_line(last_relative_x, last_relative_y, pic_file[index], last_relative_y);
                    last_relative_x = pic_file[index];
                    index += 1;
                    if ((pic_file[index] & 0xf0) == 0xf0) {
                        break;
                    }
                    draw_line(last_relative_x, last_relative_y, last_relative_x, pic_file[index]);
                    last_relative_y = pic_file[index];
                    index += 1;
                }
                break;
            case 0xF6:
                pset(pic_file[index], pic_file[index + 1]);
                while (1) {
                    if ((pic_file[index + 2] >= 0xf0) || (pic_file[index + 3] >= 0xf0)) break;
                    draw_line(pic_file[index], pic_file[index+1], pic_file[index+2], pic_file[index+3]);
                    index += 2;
                } 
                index += 2;
                break;
            case 0xF7:
                last_relative_x = pic_file[index];
                last_relative_y = pic_file[index+1];
                pset(pic_file[index], pic_file[index+1]);
                index += 2;
                while ((pic_file[index] & 0xf0) != 0xf0) {
                    draw_relative_line(pic_file[index]);
                    index += 1;
                };
                break;
            case 0xF8:
                while ((pic_file[index] & 0xf0) != 0xf0) {
                    draw_fill(pic_file[index], pic_file[index+1]);
                    index += 2;
                }
                break;
            case 0xFA:
            case 0xFF:
                index = 0xffff;
                break;
            default:
                return;
        }
    } while (index < pic_descriptors[pic_num].length);
    pic_color = 0;
    pic_on = 1;
    priority_color = 4;
    priority_on = 1;
    draw_line(0, 167, 159, 167);
}

void pic_add_to_pic(uint8_t pic_command) {
    view_set(&object_view, add_to_pic_commands[pic_command].view_number);
    select_loop(&object_view, add_to_pic_commands[pic_command].loop_index);
    object_view.cel_index = add_to_pic_commands[pic_command].cel_index;
    object_view.x_pos = add_to_pic_commands[pic_command].x_pos;
    object_view.y_pos = add_to_pic_commands[pic_command].y_pos;
    object_view.priority_override = true;
    object_view.priority = add_to_pic_commands[pic_command].priority;
    object_view.priority_set = true;

    select_sprite_mem();
    sprite_draw_to_pic(false);
    select_engine_logichigh_mem();
    if (add_to_pic_commands[pic_command].margin < 4) {
        uint8_t y2 = 0;
        for (uint8_t i = 11; i > 0; i--) {
            if (priority_bands[i-1] < object_view.y_pos){
                y2 = priority_bands[i-1];
                break;
            }
        }
        if (y2 < (object_view.y_pos - object_view.height + 1)) {
            y2 = (object_view.y_pos - object_view.height + 1);
        }
        pic_drawslowline(object_view.x_pos, object_view.y_pos, object_view.x_pos, y2, add_to_pic_commands[pic_command].margin | 0x80);
        pic_drawslowline(object_view.x_pos + object_view.width - 1, object_view.y_pos, object_view.x_pos + object_view.width - 1, y2, add_to_pic_commands[pic_command].margin | 0x80);
        pic_drawslowline(object_view.x_pos, object_view.y_pos, object_view.x_pos + object_view.width - 1,  object_view.y_pos, add_to_pic_commands[pic_command].margin | 0x80);
        pic_drawslowline(object_view.x_pos, y2, object_view.x_pos + object_view.width - 1,  y2, add_to_pic_commands[pic_command].margin | 0x80);
    }
}

void pic_show_priority(void) {
    drawing_screen = 0;
    viewing_screen = 0;

    for (uint8_t col = 0; col < 160; col++) {
        for (uint8_t row = 0; row < 168; row++) {
            gfx_plotput(col, row, gfx_getprio(col, row));
        }
    }
    VICIV.bordercol = COLOR_RED;
    while(1);
}

#pragma clang section bss="banked_bss" data="ls_spritedata" rodata="ls_spriterodata" text="ls_spritetext"

void pic_load(uint8_t pic_num) {
    uint16_t length;
    pic_descriptors[pic_num].offset = volume_load_object(voPic, pic_num, &length);
    pic_descriptors[pic_num].length = length;
    if (pic_descriptors[pic_num].offset == 0) {
        textscr_print_ascii(0, 0, (uint8_t *)"FAULT: Failed to load pic %d.", pic_num);
        return;
    }
}
