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

#include "dialog.h"
#include "engine.h"
#include "gamesave.h"
#include "gfx.h"
#include "irq.h"
#include "main.h"
#include "memmanage.h"
#include "disk.h"
#include "mapper.h"
#include "pic.h"

// Saveable data
char game_id[8];

uint16_t chipmem_allocoffset;
uint16_t chipmem_lockoffset;
uint32_t atticmem_allocoffset;

bool input_ok;
bool player_control; 
uint8_t horizon_line;
uint8_t block_active, block_x1, block_y1, block_x2, block_y2;

uint8_t animated_sprite_count;
uint16_t free_point;

#pragma clang section bss="extradata"
__far agisprite_t sprites[256];
__far uint8_t animated_sprites[256];
__far uint16_t views[256];
__far logic_info_t logic_infos[256];
__far uint8_t object_locations[256];
__far uint8_t views_in_pic;
__far add_to_pic_command_t add_to_pic_commands[16];
#pragma clang section bss=""

uint8_t __huge *gamesave_cache;
char *gamesaveid = "MASFV111";

#pragma clang section bss="banked_bss" data="gamesave_data" rodata="gamesave_rodata" text="gamesave_text"

uint32_t gamesave_save_to_attic(void) {
    VICIV.bordercol = COLOR_GREEN;
    gamesave_cache = attic_memory + atticmem_allocoffset;
    for (int i = 0; i < 8; i++) {
        gamesave_cache[i] = gamesaveid[i];
    }

    for (int i = 0; i < 8; i++) {
        gamesave_cache[i+8] = game_id[i];
    }

    gamesave_cache[16] = chipmem_allocoffset & 0xff;
    gamesave_cache[17] = (chipmem_allocoffset >> 8) & 0xff;
    gamesave_cache[18] = chipmem_lockoffset & 0xff;
    gamesave_cache[19] = (chipmem_lockoffset >> 8) & 0xff;
    gamesave_cache[20] = 0;
    gamesave_cache[21] = 0;
    gamesave_cache[22] = atticmem_allocoffset & 0xff;
    gamesave_cache[23] = (atticmem_allocoffset >> 8) & 0xff;
    gamesave_cache[24] = (atticmem_allocoffset >> 16) & 0xff;
    gamesave_cache[25] = (atticmem_allocoffset >> 24) & 0xff;
    gamesave_cache[26] = input_ok;
    gamesave_cache[27] = player_control;
    gamesave_cache[28] = horizon_line;
    gamesave_cache[29] = block_active;
    gamesave_cache[30] = block_x1;
    gamesave_cache[31] = block_y1;
    gamesave_cache[32] = block_x2;
    gamesave_cache[33] = block_y2;
    gamesave_cache[34] = animated_sprite_count;
    gamesave_cache[35] = free_point & 0xff;
    gamesave_cache[36] = (free_point >> 8) & 0xff;
    gamesave_cache[37] = views_in_pic;

    uint32_t offset = 38;
    memmanage_memcpy_far_huge(&gamesave_cache[offset], (uint8_t __far *)sprites, sizeof(sprites));
    offset += sizeof(sprites);
    memmanage_memcpy_far_huge(&gamesave_cache[offset], (uint8_t __far *)animated_sprites, sizeof(animated_sprites));
    offset += sizeof(animated_sprites);
    memmanage_memcpy_far_huge(&gamesave_cache[offset], (uint8_t __far *)views, sizeof(views));
    offset += sizeof(views);
    memmanage_memcpy_far_huge(&gamesave_cache[offset], (uint8_t __far *)logic_infos, sizeof(logic_infos));
    offset += sizeof(logic_infos);
    memmanage_memcpy_far_huge(&gamesave_cache[offset], (uint8_t __far *)object_locations, sizeof(object_locations));
    offset += sizeof(object_locations);
    memmanage_memcpy_far_huge(&gamesave_cache[offset], (uint8_t __far *)add_to_pic_commands, sizeof(add_to_pic_commands));
    offset += sizeof(add_to_pic_commands);
    memmanage_memcpy_far_huge(&gamesave_cache[offset], (uint8_t __far *)pic_descriptors, sizeof(pic_descriptors));
    offset += sizeof(pic_descriptors);
    for (uint8_t i = 0; i < menu_opts_used; i++) {
        gamesave_cache[offset] = menu_entries[i].enabled;
        offset++;
    }

    memmanage_memcpy_far_huge(&gamesave_cache[offset], chipmem_base, 0x10000);
    offset += 0x10000;

    VICIV.bordercol = COLOR_BLACK;
    return offset;
}

uint8_t gamesave_save_to_disk(char *filename) {
    uint32_t save_size = gamesave_save_to_attic();
    strcat(filename, ".AGI");

    select_engine_diskdriver_mem();
    uint8_t errcode = disk_save_attic(filename, atticmem_allocoffset, save_size, 9);
    select_engine_enginehigh_mem();
    return errcode;
}


uint8_t gamesave_load_from_attic(void) {
    for (int i = 0; i < 8; i++) {
        if (gamesave_cache[i] != gamesaveid[i]) {
            return 254;
        }
    }

    for (int i = 0; i < 8; i++) {
        if (gamesave_cache[i+8] != game_id[i]) {
            return 255;
        }
    }

    VICIV.bordercol = COLOR_GREEN;
    chipmem_allocoffset = gamesave_cache[16];
    chipmem_allocoffset |= (gamesave_cache[17] << 8);

    chipmem_lockoffset = gamesave_cache[18];
    chipmem_lockoffset |= (gamesave_cache[19] << 8);

    uint32_t temp;
    temp = gamesave_cache[22];
    atticmem_allocoffset = temp;
    temp = gamesave_cache[23];
    atticmem_allocoffset |= (temp << 8);
    temp = gamesave_cache[24];
    atticmem_allocoffset |= (temp << 16);
    temp = gamesave_cache[25];
    atticmem_allocoffset |= (temp << 24);

    input_ok = gamesave_cache[26];
    player_control = gamesave_cache[27];
    horizon_line = gamesave_cache[28];
    block_active = gamesave_cache[29];
    block_x1 = gamesave_cache[30];
    block_y1 = gamesave_cache[31];
    block_x2 = gamesave_cache[32];
    block_y2 = gamesave_cache[33];
    animated_sprite_count = gamesave_cache[34];
    free_point = gamesave_cache[35];
    free_point |= (gamesave_cache[36] << 8);
    views_in_pic = gamesave_cache[37];

    uint32_t offset = 38;
    memmanage_memcpy_huge_far((uint8_t __far *)sprites, &gamesave_cache[offset], sizeof(sprites));
    offset += sizeof(sprites);
    memmanage_memcpy_huge_far((uint8_t __far *)animated_sprites, &gamesave_cache[offset], sizeof(animated_sprites));
    offset += sizeof(animated_sprites);
    memmanage_memcpy_huge_far((uint8_t __far *)views, &gamesave_cache[offset], sizeof(views));
    offset += sizeof(views);
    memmanage_memcpy_huge_far((uint8_t __far *)logic_infos, &gamesave_cache[offset], sizeof(logic_infos));
    offset += sizeof(logic_infos);
    memmanage_memcpy_huge_far((uint8_t __far *)object_locations, &gamesave_cache[offset], sizeof(object_locations));
    offset += sizeof(object_locations);
    memmanage_memcpy_huge_far((uint8_t __far *)add_to_pic_commands, &gamesave_cache[offset], sizeof(add_to_pic_commands));
    offset += sizeof(add_to_pic_commands);
    memmanage_memcpy_huge_far((uint8_t __far *)pic_descriptors, &gamesave_cache[offset], sizeof(pic_descriptors));
    offset += sizeof(pic_descriptors);
    for (uint8_t i = 0; i < menu_opts_used; i++) {
        menu_entries[i].enabled = gamesave_cache[offset];
        offset++;
    }

    memmanage_memcpy_huge_far(chipmem_base, &gamesave_cache[offset], 0x10000);
    logic_set_flag(12);

    gfx_hold_flip(true);
    engine_bridge_pic_load(logic_vars[0]);
    engine_bridge_draw_pic(logic_vars[0], true);
    chipmem_free(pic_descriptors[logic_vars[0]].offset);
    for (int i = 0; i < views_in_pic; i++) {
        if ((add_to_pic_commands[i].x_pos == 0xff) && (add_to_pic_commands[i].y_pos == 0xff)) {
            engine_bridge_draw_pic(logic_vars[add_to_pic_commands[i].view_number], false);
        } else {
            engine_bridge_add_to_pic(i);
        }
    }
    gfx_hold_flip(false);
    VICIV.bordercol = COLOR_BLACK;
    return 0;
}

uint8_t gamesave_load_from_disk(char *filename) {
    uint32_t gamesave_offset = atticmem_allocoffset;
    gamesave_cache = attic_memory + gamesave_offset;
    strcat(filename, ".AGI");
    uint32_t data_size;
    select_engine_diskdriver_mem();
    static uint8_t load_errcode;
    load_errcode = disk_load_attic(filename, &data_size, 9);
    select_engine_enginehigh_mem();

    if (load_errcode != 0) {
        return load_errcode;
    } else {
        uint8_t result = gamesave_load_from_attic();
        atticmem_free(gamesave_offset);
        return result;
    }
}
