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
#include "irq.h"
#include "logic.h"
#include "main.h"
#include "mapper.h"
#include "memmanage.h"
#include "sprite.h"
#include "textscr.h"
#include "view.h"
#include "gfx.h"

static void sprite_update_sprite(uint8_t sprite_num);
static bool sprite_move_at_speed(agisprite_t *sprite);
bool sprite_draw_animated(void);
void sprite_erase_animated(void);

#pragma clang section bss="banked_bss" data="hs_spritedata" rodata="hs_spriterodata" text="hs_spritetext"

view_info_t object_view;
static bool sprite_alarm = false;
static bool sprite_onland = false;
 
void sprite_draw_to_pic(bool force_draw) {
    bool old_hold = gfx_hold_flip(true);
    if (drawing_screen == force_draw) { 
        select_graphics0_mem();
    } else {
        select_graphics1_mem();
    }
    
    draw_cel(&object_view, object_view.cel_index);
    gfx_hold_flip(old_hold);
    select_sprite_mem();
}

void sprite_erase_from_pic(bool force_draw) {
    bool old_hold = gfx_hold_flip(true);
    if (drawing_screen == force_draw) { 
        select_graphics0_mem();
    } else {
        select_graphics1_mem();
    }
    
    erase_view(&object_view);
    gfx_hold_flip(old_hold);
    select_sprite_mem();
}

bool sprite_draw_animated(void) {
    free_point = chipmem_alloc(1);

    bool old_hold = gfx_hold_flip(true);
    if (drawing_screen == 0) { 
        select_graphics0_mem();
    } else {
        select_graphics1_mem();
    }

    bool ego_drawn = false;
    for (int i = 0; i < animated_sprite_count; i++) {
        agisprite_t sprite = sprites[animated_sprites[i]];
        if (sprite.drawable) {
            bool drew_something = draw_cel(&sprite.view_info, sprite.view_info.cel_index);
            if (sprite.ego) {
                ego_drawn = drew_something;
            }
            sprites[animated_sprites[i]] = sprite;
        }
    }

    gfx_hold_flip(old_hold);

    select_sprite_mem();

    return ego_drawn;
}

void sprite_erase_animated(void) {
    bool old_hold = gfx_hold_flip(true);
    if (drawing_screen == 0) { 
        select_graphics0_mem();
    } else {
        select_graphics1_mem();
    }

    for (int i = animated_sprite_count; i > 0; i--) {
        agisprite_t sprite = sprites[animated_sprites[i-1]];
        if (sprite.drawable) {
            erase_view(&sprite.view_info);
        }
    } 

    gfx_hold_flip(old_hold);

    select_sprite_mem();
}

#pragma clang section bss="banked_bss" data="ls_spritedata" rodata="ls_spriterodata" text="ls_spritetext"

uint8_t priority_bands[11] = {48, 60, 72, 84, 96, 108, 120, 132, 144, 156, 168};

uint8_t priorities[169];

static uint8_t wander_pointers[9][3] = {
    {1,3,7},
    {4,5,6},
    {5,6,7},
    {6,7,8},
    {1,7,8},
    {2,1,8},
    {1,2,3},
    {2,3,4},
    {3,4,5},
};

void sprite_sort(void) {
    uint8_t i=1;
    while (i < animated_sprite_count) {
        uint8_t x = animated_sprites[i];
        uint8_t j = i;
        while (j > 0) {
            uint8_t y = animated_sprites[j - 1];
            uint8_t px = sprites[x].view_info.priority;
            uint8_t py = sprites[y].view_info.priority;
            if (py < px) {
                break;
            }
            if (py == px) {
                uint8_t hx = sprites[x].view_info.y_pos;
                uint8_t hy = sprites[y].view_info.y_pos;
                if (hy <= hx) {
                    break;
                }
            }
            animated_sprites[j] = animated_sprites[j - 1];
            j--;
        }
        animated_sprites[j] = x;
        i++;
    }
}

void setup_priorities(void) {
    uint8_t priority_level = 4;
    for (uint8_t counter = 0; counter < 169; counter++) {
        switch(counter) {
            case 168:
            case 156:
            case 144:
            case 132:
            case 120:
            case 108:
            case 96:
            case 84:
            case 72:
            case 60:
            case 48:
                priority_level++;
                break;
        }
        priorities[counter] = priority_level;
    }
}

bool sprite_move_at_speed(agisprite_t *sprite) {

    int16_t x1 = sprite->view_info.x_pos;
    int16_t y1 = sprite->view_info.y_pos;
    int16_t x2 = sprite->prg_x_destination;
    int16_t y2 = sprite->prg_y_destination;
    int16_t speed = sprite->prg_speed;
    int16_t dist = sprite->prg_distance;

    int16_t dx = x2 - x1;
    int16_t dy = y2 - y1;
    uint16_t sqr_dx = dx * dx;
    uint16_t sqr_dy = dy * dy;

    int16_t distance_squared = sqr_dx + sqr_dy;
    
    // Check if z exceeds the distance
    if (dist >= distance_squared) {
        // We are within the target distance, so finish the move
        if (sprite->prg_complete_flag > 0) {
            logic_set_flag(sprite->prg_complete_flag);
        }
        if (sprite->ego) {
            player_control = true;
        }
        sprite->prg_speed = 0;
        sprite->object_dir = 0;
        sprite->prg_movetype = pmmNone;
        return true;
    }

    if (abs(dy) < speed) {
        sprite->object_dir = (dx > 0) ? 3 : 7; // Right or Left
    } else if (abs(dx) < speed) {
        sprite->object_dir = (dy > 0) ? 5 : 1; // Down or Up
    } else {
        if (dx > 0 && dy > 0) sprite->object_dir = 4; // Down-Right
        if (dx > 0 && dy < 0) sprite->object_dir = 2; // Up-Right
        if (dx < 0 && dy > 0) sprite->object_dir = 6; // Down-Left
        if (dx < 0 && dy < 0) sprite->object_dir = 8; // Up-Left
    }
    return false;
}

void autoselect_loop(agisprite_t *sprite) {
    if (sprite->view_info.number_of_loops == 1) {
        return;
    }
    uint8_t new_loop = 0xff;

    if (sprite->view_info.number_of_loops < 4) {
        uint8_t loop_indexes[9] = {255, 255, 0, 0, 0, 255, 1, 1, 1};
        new_loop = loop_indexes[sprite->object_dir];
    } else {
        uint8_t loop_indexes[9] = {255, 3, 0, 0, 0, 2, 1, 1, 1};
        new_loop = loop_indexes[sprite->object_dir];
    }
    if (new_loop != 255) {
        if (new_loop != sprite->view_info.loop_index) {
            sprite->view_info.loop_index = new_loop;
            select_loop(&sprite->view_info, new_loop);
        }
    }
}

uint8_t sprite_checkpos(agisprite_t *sprite, int16_t new_xpos, int16_t new_ypos) {
    uint8_t object_border = 0;
    if (new_xpos <= -1) {
        object_border = 4;
    } else if (new_xpos > (160 - sprite->view_info.width)) {
        object_border = 2;
    } 
    
    if ((new_ypos - sprite->view_info.height) <= -1) {
        object_border = 1;
    } else if (sprite->observe_horizon && (new_ypos <= horizon_line)) {
        object_border = 1;
    } else if (new_ypos > 167) {
        object_border = 3;
    }
    return object_border;
}

bool sprite_checkpri(agisprite_t *sprite, int16_t new_xpos, int16_t new_ypos) {
    bool control_line = false;
    bool water_prio = false;
    if (sprite->view_info.priority != 0x0f) {
        for (int16_t control_xpos = new_xpos; control_xpos < (new_xpos + sprite->view_info.width - 1); control_xpos++) {
            uint8_t control_prio = gfx_getprio(control_xpos, new_ypos);
            if (control_prio < 4) {
                if (control_prio == 0) {
                    sprite->object_dir = 0;
                    control_line = true;
                } else if (control_prio == 1) {
                    if (sprite->observe_blocks) {
                        sprite->object_dir = 0;
                        control_line = true;
                    }
                } else if (control_prio == 2) {
                    sprite_alarm = true;
                } else if (control_prio == 3) {
                    water_prio = true;
                }
            }
            if (control_prio != 3) {
                sprite_onland = true;
            }
        }

        if (!control_line) {
            if (!water_prio && sprite->on_water) {
                control_line = true;
            } else if (water_prio && sprite->on_land) {
                control_line = true;
            }
        }

        if (block_active) {
            if (sprite->observe_blocks) {
                bool sprite_entered = ((new_xpos < block_x2) && (new_xpos > block_x1) && (new_ypos >= block_y1) && (new_ypos <= block_y2));
                if (sprite_entered) {
                    control_line = true;
                    sprite->object_dir = 0;
                }
            }
        }
    } else {
        sprite_onland = true;
    }
    return control_line;
}

bool sprite_checkcol(agisprite_t *sprite, uint8_t spr_num, int16_t new_xpos, int16_t new_ypos) {
    bool collided = false;
    if (sprite->observe_object_collisions) {
        for (int i = 0; i < animated_sprite_count; i++) {
            if (animated_sprites[i] == spr_num) continue;
            if (!sprites[animated_sprites[i]].drawable) continue;
            if (!sprites[animated_sprites[i]].updatable) continue;
            int16_t other_x = sprites[animated_sprites[i]].view_info.x_pos;
            if ((new_xpos > (other_x+sprites[animated_sprites[i]].view_info.width)) || (new_xpos + sprite->view_info.width < other_x)) {
                continue;
            }
            int16_t other_y = sprites[animated_sprites[i]].view_info.y_pos;
            if (new_ypos != other_y) {
                continue;
            }
            collided = true;
            sprite->object_dir = 0;
            break;
        }
    }
    return collided;
}

uint8_t sprite_move(uint8_t spr_num, agisprite_t *sprite, uint8_t speed) {
    int16_t view_dx;
    int16_t view_dy;
    if (sprite->frozen) {
        if (sprite->ego) {
            logic_vars[6] = 0;
        }
        return false;
    }
    switch (sprite->object_dir) {
        case 0:
        view_dx = 0;
        view_dy = 0;
        break;
        case 1:
        view_dx = 0;
        view_dy = -speed;
        break;
        case 2:
        view_dx = speed;
        view_dy = -speed;
        break;
        case 3:
        view_dx = speed;
        view_dy = 0;
        break;
        case 4:
        view_dx = speed;
        view_dy = speed;
        break;
        case 5:
        view_dx = 0;
        view_dy = speed;
        break;
        case 6:
        view_dx = -speed;
        view_dy = speed;
        break;
        case 7:
        view_dx = -speed;
        view_dy = 0;
        break;
        case 8:
        view_dx = -speed;
        view_dy = -speed;
        break;
    }

    uint8_t object_border = 0;
    int16_t new_xpos = sprite->view_info.x_pos + view_dx;
    int16_t new_ypos = sprite->view_info.y_pos + view_dy;
    object_border = sprite_checkpos(sprite, new_xpos, new_ypos);
    switch (object_border) {
        case 1:
        case 3:
            new_ypos = sprite->view_info.y_pos;
            view_dy = 0;
            break;
        case 2:
        case 4:
            new_xpos = sprite->view_info.x_pos;
            view_dx = 0;
            break;
    }
    if ((view_dx == 0) && (view_dy == 0)) {
        sprite->object_dir = 0;
    }

    sprite_alarm = false;
    sprite_onland = false;
    sprite_checkpri(sprite, new_xpos, new_ypos);

    sprite_checkcol(sprite, spr_num, new_xpos, new_ypos);
    
    if (sprite_alarm) {
        if (sprite->ego) {
            logic_set_flag(3);
        } 
    } else {
        if (sprite->ego) {
            logic_reset_flag(3);
        } 
    }

    if (sprite_onland) {
        if (sprite->ego) {
            logic_reset_flag(0);
        } 
        if (sprite->on_water) {
            sprite->object_dir = 0;
        }
    } else {
        if (sprite->ego) {
            logic_set_flag(0);
        } 
        if (sprite->on_land) {
            sprite->object_dir = 0;
        }
    } 

    if (sprite->ego) {
        logic_vars[2] = object_border;
        logic_vars[6] = sprite->object_dir;
    } else {
        if (object_border != 0) {
            logic_vars[4] = spr_num;
            logic_vars[5] = object_border;
        }
    }
    
    if (sprite->object_dir != 0) {
        sprite->view_info.x_pos = sprite->view_info.x_pos + view_dx;
        sprite->view_info.y_pos = sprite->view_info.y_pos + view_dy;
        return true;
    }

    sprite->object_dir = 0;
    return false;
}

void sprite_stop_all(void) {

}

void sprite_unanimate_all(void) {
    for (uint16_t sprite_num = 0; sprite_num < 256; sprite_num++) {
        sprites[sprite_num].drawable = false;
        sprites[sprite_num].view_info.priority_override = false;
        sprites[sprite_num].cycling = true;
        sprites[sprite_num].observe_horizon = true;
        sprites[sprite_num].observe_blocks = true;
        sprites[sprite_num].updatable = true;
        sprites[sprite_num].step_size = 1;
        sprites[sprite_num].step_time = 1;
        sprites[sprite_num].step_count = 0;
        sprites[sprite_num].loop_override = false;
        if (sprite_num == 0) {
            sprites[sprite_num].ego = true;
        } else {
            sprites[sprite_num].ego = false;
        }
        sprites[sprite_num].prg_movetype = pmmNone;
        sprites[sprite_num].on_water = false;
        sprites[sprite_num].on_land = false;
        sprites[sprite_num].object_dir = 0;
        animated_sprites[sprite_num] = 0;
    }
    animated_sprite_count = 0;
}

void sprite_mark_drawable(uint8_t sprite_num) {
    agisprite_t sprite = sprites[sprite_num];
    agisprite_t *testspr = &sprite;
    sprite.drawable = true;
    sprite.updatable = true;
    if (sprite_num == 0) {
        logic_reset_flag(0);
        logic_reset_flag(1);
        logic_reset_flag(3);
    }
    if (sprite.observe_horizon) {
        if (sprite.view_info.y_pos <= horizon_line) {
                sprite.view_info.y_pos = horizon_line + 1;
        }
    }
    
    uint8_t dir = 0;
    uint8_t count = 1;
    uint8_t size = 1;
    int16_t xpos = sprite.view_info.x_pos;
    int16_t ypos = sprite.view_info.y_pos;
    while ((sprite_checkpos(testspr, xpos, ypos) != 0) || (sprite_checkpri(testspr, xpos, ypos)) || (sprite_checkcol(testspr, sprite_num, xpos, ypos))) {
		switch (dir) {
		case 0: // west
            xpos--;
			if (--count)
				continue;
			dir = 1;
			break;
		case 1: // south
            ypos++;
			if (--count)
				continue;
			dir = 2;
			size++;
			break;
		case 2: // east
            xpos++;
			if (--count)
				continue;
			dir = 3;
			break;
		case 3: // north
            ypos--;
			if (--count)
				continue;
			dir = 0;
			size++;
			break;
		default:
			break;
		}

		count = size;
    }
    sprite.view_info.x_pos = xpos;
    sprite.view_info.y_pos = ypos;
    sprites[sprite_num] = sprite;
}

void sprite_setedge(uint8_t sprite_num, uint8_t edgenum) {
    switch(edgenum) {
        case 1:
            sprites[sprite_num].view_info.y_pos = 166;
            break;
        case 2:
            sprites[sprite_num].view_info.x_pos = 0;
            break;
        case 3:
            sprites[sprite_num].view_info.y_pos = horizon_line + 1;
            break;
        case 4:
            sprites[sprite_num].view_info.x_pos = 160 - sprites[sprite_num].view_info.width;
            break;
    }
}

void sprite_set_view(uint8_t sprite_num, uint8_t view_number) {
    agisprite_t sprite = sprites[sprite_num];

    sprite.cycle_time = 1;
    sprite.cycle_count = 1;
    sprite.end_of_loop = 0;
    sprite.reverse = false;
    view_set(&sprite.view_info, view_number);
    if (sprite.view_info.loop_index >= sprite.view_info.number_of_loops) {
        sprite.view_info.loop_index = 0;
    }
    select_loop(&sprite.view_info, sprite.view_info.loop_index);
    sprite_set_cel(&sprite, sprite.view_info.cel_index);
    sprites[sprite_num] = sprite;
}

void sprite_set_cel(agisprite_t *sprite, uint8_t cel_number) {
    sprite->view_info.cel_index = cel_number;
    if (sprite->observe_horizon) {
        if (sprite->view_info.y_pos <= horizon_line) {
                sprite->view_info.y_pos = horizon_line + 1;
        }
    }

    if (views[sprite->view_info.view_index] == 0) {
        return;
    }
    
    uint8_t __far *loop_data = chipmem_base + sprite->view_info.loop_offset;
    uint8_t __far *cell_ptr = loop_data + (cel_number * 2) + 1;
    uint16_t cel_offset = (*cell_ptr | ((*(cell_ptr + 1)) << 8));

    uint8_t __far *cel_data = chipmem_base + cel_offset;
    sprite->view_info.width  = cel_data[0];
    sprite->view_info.height = cel_data[1];
    if (sprite->view_info.y_pos - sprite->view_info.height + 1 <= 0) {
        sprite->view_info.y_pos = sprite->view_info.height - 1;
    }

    if (sprite->view_info.x_pos + sprite->view_info.width >= 160) {
        sprite->view_info.x_pos = 160 - sprite->view_info.width;
    }
}

uint8_t sprite_get_view(uint8_t sprite_num) {
    return sprites[sprite_num].view_info.view_index;
}

void sprite_update_sprite(uint8_t sprite_num) {
    agisprite_t sprite = sprites[sprite_num]; 
    sprite.step_count++;
    if (sprite.step_count >= sprite.step_time) {
        sprite.step_count = 0;
        uint8_t speed = 1;
        switch (sprite.prg_movetype) {
            case pmmNone:
                speed = sprite.step_size;
            break;
            case pmmFollowWander:
                sprite.prg_timer++;
                if (sprite.prg_timer >= 5) {
                    sprite.prg_movetype = pmmFollow;
                    speed = sprite.prg_speed;
                    sprite.prg_x_destination = sprites[0].view_info.x_pos;
                    sprite.prg_y_destination = sprites[0].view_info.y_pos;
                    sprite_move_at_speed(&sprite);
                }
            case pmmWander:
                speed = sprite.step_size;
                if (sprite.object_dir == 0)
                {
                    uint8_t rand_dir = (abs(rand()) % 3);
                    uint8_t new_dir = wander_pointers[sprite.prg_dir][rand_dir];
                    sprite.object_dir = new_dir;
                    sprite.prg_dir = new_dir;
                }
            break;
            case pmmMoveTo:
                speed = sprite.prg_speed;
                sprite_move_at_speed(&sprite);
            break;
            case pmmFollow:
                if (sprite.object_dir == 0) {
                    sprite.prg_movetype = pmmFollowWander;
                    sprite.prg_timer = 0;
                } else {
                    speed = sprite.prg_speed;
                    sprite.prg_x_destination = sprites[0].view_info.x_pos;
                    sprite.prg_y_destination = sprites[0].view_info.y_pos;
                    sprite_move_at_speed(&sprite);
                }
            break;
            case pmmFollowInit:
                speed = sprite.prg_speed;
                sprite.prg_x_destination = sprites[0].view_info.x_pos;
                sprite.prg_y_destination = sprites[0].view_info.y_pos;
                sprite_move_at_speed(&sprite);
                sprite.prg_movetype = pmmFollow;
            break;
            default:
            break;
        }

        sprite_move(sprite_num, &sprite, speed);
        if (!sprite.view_info.priority_override) {
            sprite.view_info.priority = priorities[sprite.view_info.y_pos];
        }

        if (!sprite.loop_override) {
            autoselect_loop(&sprite);
        }
    }

    sprite.cycle_count--;
    if (sprite.cycle_count == 0) {
        sprite.cycle_count  = sprite.cycle_time;
        if (sprite.cycling) {
            if (sprite.reverse) {
                if (sprite.view_info.cel_index == 0) {
                    sprite_set_cel(&sprite, sprite.view_info.number_of_cels);
                } else {
                    sprite_set_cel(&sprite, sprite.view_info.cel_index - 1);
                }

            } else {
                if (sprite.view_info.cel_index >= (sprite.view_info.number_of_cels - 1)) {
                    sprite_set_cel(&sprite, 0);
                } else {
                    sprite_set_cel(&sprite, sprite.view_info.cel_index + 1);
                } 
            }
        }
        if (sprite.end_of_loop > 0) {
            if (sprite.reverse) {
                if (sprite.view_info.cel_index == 0) {
                    logic_set_flag(sprite.end_of_loop);
                    sprite.end_of_loop = 0;
                    sprite.cycling = false;
                }
            } else {
                if (sprite.view_info.cel_index == (sprite.view_info.number_of_cels - 1)) {
                    logic_set_flag(sprite.end_of_loop);
                    sprite.end_of_loop = 0;
                    sprite.cycling = false;
                }
            }
        }
    }
    sprites[sprite_num] = sprite;
}

void sprite_clearall(void) {
    uint8_t __far *sprdatptr = (uint8_t __far *)sprites;
    for (int i = 0; i < sizeof(sprites); i++) {
        sprdatptr[i] = 0;
    }
}

void sprite_undraw(void) {
    select_sprite_mem();
    sprite_erase_animated();
    select_engine_logiclow_mem();
    if (free_point > 0) {
        chipmem_free(free_point);
    }
}

void sprite_updateanddraw(void) {
    for (int i = 0; i < animated_sprite_count; i++) {
        if (sprites[animated_sprites[i]].updatable) {
            sprite_update_sprite(animated_sprites[i]);
        }
    }

    sprite_sort();

    select_sprite_mem();

    bool ego_drawn = sprite_draw_animated();

    select_engine_logiclow_mem();

    if (ego_drawn) {
        logic_reset_flag(1);
    } else {
        logic_set_flag(1);
    }

}

void sprite_show_object(uint8_t view_num) {
    view_load(view_num);
    view_set(&object_view, view_num);
    select_loop(&object_view, 0);
    object_view.x_pos = 80 - (object_view.width / 2);
    object_view.y_pos = 155;
    object_view.priority_override = true;
    object_view.priority = 0x0f;
    uint8_t __far *desc_data = chipmem_base + object_view.desc_offset;
    select_sprite_mem();
    sprite_draw_to_pic(true);
    select_engine_logichigh_mem();
    engine_bridge_dialog_show(false, false, false, desc_data);
    select_sprite_mem();
    sprite_erase_from_pic(true);
    select_engine_logichigh_mem();
}

void sprite_init(void) {
    setup_priorities();
    animated_sprite_count = 0;
    free_point = 0;
    sprite_clearall();
    sprite_unanimate_all();
}