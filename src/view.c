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
#include <stdbool.h>
#include <string.h>
#include <calypsi/intrinsics6502.h>
#include <mega65.h>
#include <math.h>

#include "gfx.h"
#include "main.h"
#include "memmanage.h"
#include "volume.h"
#include "view.h"
#include "irq.h"
#include "textscr.h"
#include "mapper.h"

#pragma clang section bss="banked_bss" data="hs_spritedata" rodata="hs_spriterodata" text="hs_spritetext"

static const uint8_t colorval[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static const uint8_t high_nibble[256] = {
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1,
    0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2, 0x2,
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3,
    0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4,
    0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5,
    0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6, 0x6,
    0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7,
    0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
    0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
    0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa, 0xa,
    0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb,
    0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc, 0xc,
    0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd,
    0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe,
    0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf
};

static uint8_t drawview_trans;
static int16_t drawview_left;
static int16_t drawview_top;
static uint8_t drawview_right;
static uint8_t drawview_bottom;
static uint8_t objprio;
static uint8_t prio_mask;
static uint8_t prio_preserve_mask;
static uint8_t prio_threshold;
static uint8_t prio_compare;
static uint8_t rle_color;
static uint8_t rle_count;

bool draw_cel_forwards(view_info_t *info, uint8_t cel) {
    uint8_t __far *loop_data = chipmem_base + info->loop_offset;
    uint8_t __far *cell_ptr = loop_data + (cel * 2) + 1;
    uint16_t cel_offset = (*cell_ptr | ((*(cell_ptr + 1)) << 8));

    uint8_t __far *cel_data = chipmem_base + cel_offset;

    drawview_trans = colorval[cel_data[2] & 0x0f];
    drawview_left = info->x_pos;
    drawview_top = info->y_pos - info->height + 1;
    drawview_right = info->x_pos + info->width;
    drawview_bottom = info->y_pos;

    objprio = colorval[info->priority];

    cel_data += 3;
    bool drew_something = false;;
    for (int16_t draw_col = drawview_left; draw_col < drawview_right; draw_col++) {
        if ((draw_col == 0) || (draw_col > 158)) {
            continue;
        }
        volatile uint8_t *draw_pointer = fastdrawing_xpointer[draw_col] + (drawview_top * 8);
        uint8_t xcolumn = draw_col >> 1;
        uint8_t highpix = draw_col & 1;
        volatile uint8_t __far *priority_pointer = &priority_screen[(drawview_top * 80) + xcolumn];
        uint8_t prioval;

        if (highpix) {
            prio_mask = 0xf0;
            prio_preserve_mask = 0x0f;
            prio_threshold = 0x40;
        } else {
            prio_mask = 0x0f;
            prio_preserve_mask = 0xf0;
            prio_threshold = 4;
        }

        // Mask the priority values once
        prio_compare = objprio & prio_mask;

        for (int16_t draw_row = drawview_top; draw_row <= drawview_bottom;) {
            if ((draw_row < 0) || (draw_row > 167)) {
                priority_pointer += 80;
                draw_row++;
                continue;
            }
            rle_count = high_nibble[*cel_data];
            rle_color = colorval[*cel_data & 0x0f];

            for (uint8_t counter = 0; counter < rle_count; counter++) {
                volatile uint8_t __far *scanprio_pointer = priority_pointer;
                do {
                    prioval = *scanprio_pointer & prio_mask;
                    scanprio_pointer += 80;
                } while (prioval < prio_threshold);
                priority_pointer += 80;

                if (rle_color != drawview_trans) {
                    if ((prioval) <= prio_compare) {
                        *draw_pointer = rle_color;
                        drew_something=true;
                        if (info->priority_set) {
                            scanprio_pointer -= 80;
                            *scanprio_pointer = (*scanprio_pointer & prio_preserve_mask) | prio_compare;
                        }
                    }
                }
                draw_pointer += 8;
                draw_row++;
            }
            cel_data++;
        }
    }
    return drew_something;
}

bool draw_cel_backwards(view_info_t *info, uint8_t cel) {
    uint8_t __far *loop_data = chipmem_base + info->loop_offset;
    uint8_t __far *cell_ptr = loop_data + (cel * 2) + 1;
    uint16_t cel_offset = (*cell_ptr | ((*(cell_ptr + 1)) << 8));

    uint8_t __far *cel_data = chipmem_base + cel_offset;

    drawview_trans = colorval[cel_data[2] & 0x0f];
    drawview_left = info->x_pos;
    drawview_top = info->y_pos - info->height + 1;
    drawview_right = info->x_pos + info->width;
    drawview_bottom = info->y_pos;

    objprio = colorval[info->priority];

    cel_data += 3;
    bool drew_something = false;;
    for (int16_t draw_col = drawview_right-1; draw_col >= drawview_left; draw_col--) {
        if ((draw_col == 0) || (draw_col > 158)) {
            continue;
        }
        volatile uint8_t *draw_pointer = fastdrawing_xpointer[draw_col] + (drawview_top * 8);
        uint8_t xcolumn = draw_col >> 1;
        uint8_t highpix = draw_col & 1;
        volatile uint8_t __far *priority_pointer = &priority_screen[(drawview_top * 80) + xcolumn];
        uint8_t prioval;

        if (highpix) {
            prio_mask = 0xf0;
            prio_preserve_mask = 0x0f;
            prio_threshold = 0x40;
        } else {
            prio_mask = 0x0f;
            prio_preserve_mask = 0xf0;
            prio_threshold = 4;
        }

        // Mask the priority values once
        prio_compare = objprio & prio_mask;

        for (int16_t draw_row = drawview_top; draw_row <= drawview_bottom;) {
            if ((draw_row < 0) || (draw_row > 167)) {
                priority_pointer += 80;
                draw_row++;
                continue;
            }
            rle_count = high_nibble[*cel_data];
            rle_color = colorval[*cel_data & 0x0f];

            for (uint8_t counter = 0; counter < rle_count; counter++) {
                volatile uint8_t __far *scanprio_pointer = priority_pointer;
                do {
                    prioval = *scanprio_pointer & prio_mask;
                    scanprio_pointer += 80;
                } while (prioval < prio_threshold);
                priority_pointer += 80;

                if (rle_color != drawview_trans) {
                    if ((prioval) <= prio_compare) {
                        *draw_pointer = rle_color;
                        drew_something=true;
                        if (info->priority_set) {
                            scanprio_pointer -= 80;
                            *scanprio_pointer = (*scanprio_pointer & prio_preserve_mask) | prio_compare;
                        }
                    }
                }
                draw_pointer += 8;
                draw_row++;
            }
            cel_data++;
        }
    }
    return drew_something;
}

bool draw_cel(view_info_t *info, uint8_t cel) {
    uint16_t pixel_offset = 0;

    uint8_t __far *loop_data = chipmem_base + info->loop_offset;
    uint8_t __far *cell_ptr = loop_data + (cel * 2) + 1;
    uint16_t cel_offset = (*cell_ptr | ((*(cell_ptr + 1)) << 8));

    uint8_t __far *cel_data = chipmem_base + cel_offset;
    info->width  = cel_data[0];
    info->height = cel_data[1];

    drawview_left = info->x_pos;
    drawview_top = info->y_pos - info->height + 1;
    drawview_right = info->x_pos + info->width;
    drawview_bottom = info->y_pos;

    info->backbuffer_offset = chipmem_alloc(info->height * info->width);
    uint8_t __far *view_backbuf = chipmem_base + info->backbuffer_offset;

    for (int16_t draw_col = drawview_left; draw_col < drawview_right; draw_col++) {
        if ((draw_col > 0) && (draw_col < 159)) {
            volatile uint8_t *draw_pointer = fastdrawing_xpointer[draw_col] + (drawview_top * 8);
            for (int16_t draw_row = drawview_top; draw_row <= drawview_bottom; draw_row++) {
                if ((draw_row > 0) && (draw_row < 168)) {
                    view_backbuf[pixel_offset] = *draw_pointer;
                    draw_pointer += 8;
                    pixel_offset++;
                }
            }
        }
    }

    uint8_t mirrored = cel_data[2] & 0x80;
    uint8_t normal_loop = high_nibble[cel_data[2] & 0x70];
    if (mirrored && (normal_loop != info->loop_index)) {
        return draw_cel_backwards(info, cel);
    } else {
        return draw_cel_forwards(info, cel);
    }
}

void erase_view(view_info_t *info) {
    uint16_t pixel_offset = 0;
    drawview_left = info->x_pos;
    drawview_top = info->y_pos - info->height + 1;
    drawview_right = info->x_pos + info->width;
    drawview_bottom = info->y_pos;
    uint8_t __far *view_backbuf = chipmem_base + info->backbuffer_offset;

    for (int16_t draw_col = drawview_left; draw_col < drawview_right; draw_col++) {
        if ((draw_col > 0) && (draw_col < 159)) {
            volatile uint8_t *draw_pointer = fastdrawing_xpointer[draw_col] + (drawview_top * 8);
            for (int16_t draw_row = drawview_top; draw_row <= drawview_bottom; draw_row++) {
                if ((draw_row > 0) && (draw_row < 168)) {
                    *draw_pointer = view_backbuf[pixel_offset];
                    draw_pointer += 8;
                    pixel_offset++;
                }
            }
        }
    }
}

#pragma clang section bss="banked_bss" data="eh_data" rodata="eh_rodata" text="eh_text"

void unpack_view(uint8_t view_num, uint8_t __huge *view_location) {
    uint8_t loop_count = view_location[2];
    uint16_t view_offset = chipmem_alloc(1 + (loop_count * 2));

    uint8_t __far *view_dest_location = chipmem_base + view_offset;
    views[view_num] = view_offset;
    view_dest_location[0] = loop_count;
    uint8_t desc_offset_loc = 3;
    uint16_t desc_source_offset = ((view_location[desc_offset_loc + 1]) << 8);
    desc_source_offset |= view_location[desc_offset_loc];
    if (desc_source_offset != 0) {
        uint16_t desc_dest_offset = chipmem_alloc(1);
        uint16_t desc_length = 0;
        uint8_t __huge *desc_source_location = view_location + desc_source_offset;
        uint8_t __far *desc_dest_location = chipmem_base + desc_dest_offset;
        uint8_t copy_byte;
        do {
           copy_byte = *desc_source_location;
           *desc_dest_location = copy_byte;
           desc_source_location++;
           desc_dest_location++;
           desc_length++; 
        } while (copy_byte != 0);
        chipmem_free(desc_dest_offset);
        chipmem_alloc(desc_length);
    }

    uint8_t __huge *cel_atticmem_buffer_location;
    for (uint8_t loop_index = 0; loop_index < loop_count; loop_index++) {
        uint8_t loop_offset_loc = (loop_index * 2) + 5;
        uint16_t loop_source_offset = ((view_location[loop_offset_loc + 1]) << 8);
        loop_source_offset |= view_location[loop_offset_loc];
        bool match_found = false;
        for (uint8_t prev_loop_index = 0; prev_loop_index < loop_index; prev_loop_index++) {
            uint8_t prev_loop_offset_loc = (prev_loop_index * 2) + 5;
            uint16_t prev_loop_source_offset = ((view_location[prev_loop_offset_loc + 1]) << 8);
            prev_loop_source_offset |= view_location[prev_loop_offset_loc];
            if (loop_source_offset == prev_loop_source_offset) {
                view_dest_location[1 + (loop_index * 2)] = view_dest_location[1 + (prev_loop_index * 2)];
                view_dest_location[2 + (loop_index * 2)] = view_dest_location[2 + (prev_loop_index * 2)];
                match_found = true;
            }
        }
        if (match_found) {
            continue;
        }
        uint8_t __huge *loop_source_location = view_location + loop_source_offset;
        uint8_t cels_in_loop  = loop_source_location[0];
        uint16_t loop_overhead = 1 + (cels_in_loop * 2);
        uint16_t loop_dest_offset = chipmem_alloc(loop_overhead);
        view_dest_location[1 + (loop_index * 2)] = loop_dest_offset & 0xff;
        view_dest_location[2 + (loop_index * 2)] = (loop_dest_offset & 0xff00) >> 8;

        uint8_t __far *loop_dest_location = chipmem_base + loop_dest_offset;
        loop_dest_location[0] = cels_in_loop;
    
        for (uint8_t cel_index = 0; cel_index < cels_in_loop; cel_index++) {
            uint8_t cel_offset_loc = (cel_index * 2) + 1;
            uint16_t cel_source_offset = ((loop_source_location[cel_offset_loc + 1]) << 8);
            cel_source_offset |= loop_source_location[cel_offset_loc];
            uint8_t __huge *cel_source_location = loop_source_location + cel_source_offset;
            uint8_t cel_width = cel_source_location[0];
            uint8_t cel_height = cel_source_location[1];
            uint8_t cel_transparency = cel_source_location[2] & 0x0f;
            uint16_t cel_size = 3;

            cel_atticmem_buffer_location = attic_memory + atticmem_allocoffset;
        
            uint16_t cur_x = 0;
            uint16_t cur_y = 0;
            uint16_t cel_offset = 3;
            do {
                uint8_t pixcol = cel_source_location[cel_offset] >> 4;
                uint8_t pixcnt = cel_source_location[cel_offset] & 0x0f;
                if ((pixcol == 0) && (pixcnt == 0)) {
                    for (; cur_x < cel_width; cur_x++) {
                        cel_atticmem_buffer_location[(cur_x * cel_height) + cur_y] = cel_transparency;
                    }
                    cur_x = 0;
                    cur_y++;
                } else {
                    for (uint8_t count = 0; count < pixcnt; count++) {
                        cel_atticmem_buffer_location[(cur_x * cel_height) + cur_y] = pixcol;
                        cur_x++;
                    }
                }
                cel_offset++;
            } while (cur_y < cel_height);

            uint16_t cel_chipmem_dest_offset = chipmem_allocoffset;
            loop_dest_location[1 + (cel_index * 2)] = cel_chipmem_dest_offset & 0xff;
            loop_dest_location[2 + (cel_index * 2)] = (cel_chipmem_dest_offset & 0xff00) >> 8;

            uint8_t __far *cel_chipmem_dest_location = chipmem_base + cel_chipmem_dest_offset;
            cel_chipmem_dest_location[0] = cel_width;
            cel_chipmem_dest_location[1] = cel_height;
            cel_chipmem_dest_location[2] = cel_source_location[2];
            cel_chipmem_dest_location += 3;

            uint8_t color_val = 0;
            uint8_t color_len = 0;
            for (cur_x = 0; cur_x < cel_width; cur_x++) {
                for (cur_y = 0; cur_y < cel_height; cur_y++) {
                    if (color_len == 0) {
                        color_val = cel_atticmem_buffer_location[(cur_x * cel_height) + cur_y];
                        color_len = 1;
                    } else {
                        if ((color_val == cel_atticmem_buffer_location[(cur_x * cel_height) + cur_y]) && (color_len < 15)) {
                            color_len++;
                        } else {
                            *cel_chipmem_dest_location = (color_len << 4) | color_val;
                            cel_chipmem_dest_location++;
                            color_val = cel_atticmem_buffer_location[(cur_x * cel_height) + cur_y];
                            color_len = 1;
                            cel_size++;
                        }
                    }
                }
                *cel_chipmem_dest_location = (color_len << 4) | color_val;
                cel_chipmem_dest_location++;
                color_len = 0;
                cel_size++;
            }

            chipmem_alloc(cel_size);
        }
    }
}

#pragma clang section bss="banked_bss" data="ls_spritedata" rodata="ls_spriterodata" text="ls_spritetext"

bool select_loop(view_info_t *info, uint8_t loop_num) {
    uint16_t view_offset = views[info->view_index];
    if (view_offset == 0) {
        return false;
    }
    uint8_t __far *view_data = chipmem_base + view_offset;
    info->loop_index = loop_num;
    if (loop_num < info->number_of_loops) {
        uint8_t __far *loop_ptr = view_data + (loop_num * 2) + 1;
        uint16_t loop_offset = *loop_ptr | ((*(loop_ptr + 1)) << 8);
        uint8_t __far *loop_data = chipmem_base + loop_offset;
        info->loop_offset = loop_offset;
        info->number_of_cels = loop_data[0];
        if (info->cel_index >= info->number_of_cels) {
            info->cel_index = 0;
        }
        uint8_t __far *cell_ptr = loop_data + 1;
        uint16_t cel_offset = (*cell_ptr | ((*(cell_ptr + 1)) << 8));

        uint8_t __far *cel_data = chipmem_base + cel_offset;
        info->width  = cel_data[0];
        info->height = cel_data[1];
        return true;
    }
    return false;
}

void view_set(view_info_t *info, uint8_t view_num) {
    info->view_index = view_num;
    uint16_t view_offset = views[view_num];
    if (view_offset == 0) {
        return;
    }
    uint8_t __far *view_data = chipmem_base + view_offset;
    info->number_of_loops = view_data[0];
    uint8_t __far *loop_ptr = view_data + (info->loop_index * 2) + 1;
    uint16_t loop_offset = *loop_ptr | ((*(loop_ptr + 1)) << 8);
    uint8_t __far *loop_data = chipmem_base + loop_offset;
    info->number_of_cels = loop_data[0];
    info->desc_offset = view_offset + (info->number_of_loops * 2) + 1;
    info->priority_set = false;
}

bool view_load(uint8_t view_num) {
    if (views[view_num] == 0) {
        uint16_t length;
        uint8_t __huge *view_location = volume_locate_object(voView, view_num, &length);
        if (view_location == 0) {
            return false;
        }
        select_engine_enginehigh_mem();
        unpack_view(view_num, view_location);
        select_engine_logiclow_mem();
        return true;
    }
    return false;
}

void view_unload(uint8_t view_num) {
    if (views[view_num] != 0) {
        chipmem_free(views[view_num]);
        views[view_num] = 0;
    }
}

void view_purge(uint16_t freed_offset) {
    for (int i = 0; i < 256; i++) {
        if (views[i] >= freed_offset) {
            views[i] = 0;
        }
    }
}

void view_init(void) {
    for (int i = 0; i < 256; i++) {
        views[i] = 0;
    }
}
