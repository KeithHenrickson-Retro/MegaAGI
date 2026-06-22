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

#include "clock.h"
#include "gfx.h"
#include "dialog.h"
#include "irq.h"
#include "pic.h"
#include "sound.h"
#include "view.h"
#include "volume.h"
#include "parser.h"
#include "sound.h"
#include "sprite.h"
#include "textscr.h"
#include "logic.h"
#include "mapper.h"
#include "memmanage.h"
#include "mouse.h"
#include "ports.h"
#include "main.h"

volatile uint8_t frame_counter;
volatile bool run_engine;
volatile bool game_timeslot_ready;
bool quit_flag;

#pragma clang section bss="banked_bss" data="eh_data" rodata="eh_rodata" text="eh_text"
void engine_show_welcome_text(void) {
    VICIV.bordercol = COLOR_GREEN;
    textscr_set_color(COLOR_YELLOW, COLOR_BLACK);
    textscr_print_ascii(0, 16, (uint8_t *)"  In memory of Shadow Fox, dolphins in");
    textscr_print_ascii(0, 17, (uint8_t *)"   your dreams, too many other foxes,");
    textscr_print_ascii(0, 18, (uint8_t *)"and those who speak for those who can't.");
    textscr_set_color(COLOR_WHITE, COLOR_BLACK);
    textscr_print_ascii(0, 19, (uint8_t *)"   Starting up. Patience is a virtue.");
    textscr_print_ascii(0, 20, (uint8_t *)"  Green border means game is thinking!");
    textscr_print_ascii(0, 21, (uint8_t *)" 1351 mouse, port 1.  Joystick, port 2.");
    textscr_set_color(COLOR_LIGHTGREY, COLOR_BLACK);
    textscr_print_ascii(0, 23, (uint8_t *)"      Mega-AGI Copyright 2026 by");
    textscr_print_ascii(0, 24, (uint8_t *)"           Keith Henrickson");
}

void engine_showload_dialog(void) {
    memmanage_strcpy_near_far(print_string_buffer, (uint8_t *)"Loading, please wait!");
    dialog_show_enginehigh(false, false, true, print_string_buffer);
    select_volume_mem();
}

void engine_clearload_dialog(void) {
    dialog_close();
    select_volume_mem();
}

void engine_askdisk_dialog(uint8_t disk_number) {
    memmanage_strcpy_near_far(print_string_buffer, (uint8_t *)"Please insert disk %d.");
    dialog_show_enginehigh(false, false, false, print_string_buffer, disk_number);
    select_volume_mem();
}

#pragma clang section bss="banked_bss" data="enginedata" rodata="enginerodata" text="enginetext"

volatile uint8_t run_cycles;
volatile bool engine_running;
volatile bool game_timeslot_ready;
bool status_line_enabled;
uint8_t status_line_score;
bool status_line_sound;
bool mouse_down = false;

static const uint8_t joystick_direction_to_agi[16] = {
    0, 0, 0, 0, 0, 4, 2, 3,
    0, 6, 8, 7, 0, 5, 1, 0
};

void engine_bridge_add_to_pic(uint8_t add_command_num) {
    select_picdraw_mem();
    pic_add_to_pic(add_command_num);
    select_gamesave_mem();
}

void engine_bridge_pic_load(uint8_t pic_num) {
    select_picdraw_mem();
    pic_load(pic_num);
    select_gamesave_mem();
}

void engine_bridge_draw_pic(uint8_t pic_num, bool clear) {
    select_picdraw_mem();
    draw_pic(pic_num, clear);
    select_gamesave_mem();
}

bool engine_bridge_dialog_show(bool accept_input, bool ok_cancel, bool draw_only, uint8_t __far *message_string, ...) {
    bool result;
    va_list ap;
    va_start(ap, message_string);

    select_engine_enginehigh_mem();
    result = dialog_show_valist(accept_input, ok_cancel, draw_only, message_string, ap);
    select_previous_bank();
    va_end(ap);
    return result;
}

void engine_update_status_line(bool force) {
    if (!game_text) {
        if (status_line_enabled) {
            if ((logic_vars[3] != status_line_score) || (logic_flag_isset(9) != status_line_sound) || force) {
                status_line_score = logic_vars[3];
                status_line_sound = logic_flag_isset(9);
                textscr_set_color(COLOR_BLACK, COLOR_WHITE);
                if (status_line_sound) {
                    textscr_print_ascii(0, 0, (uint8_t  *)"Score: %d of %d%p27Sound: On%p40", logic_vars[3], logic_vars[7]);
                } else {
                    textscr_print_ascii(0, 0, (uint8_t  *)"Score: %d of %d%p27Sound: Off%p40", logic_vars[3], logic_vars[7]);
                }
            }
        }
    }
}

/*
    0000 = inv
    0001 = inv
    0010 = inv
    0011 = inv
    0100 = inv
    0101 = right/down
    0110 = right/up
    0111 = right
    1000 = inv
    1001 = left/down
    1010 = left/up
    1011 = left
    1100 = inv
    1101 = down
    1110 = up
    1111 = inv
*/

void handle_movement_joystick(void) {
    static bool joy_pressed = false;
    if (!player_control) {
        return;
    }
     
    uint8_t new_object_dir = joystick_direction_to_agi[joystick_direction];
    if (new_object_dir > 0) {
        if (sprites[0].object_dir == 0) {
            if (joy_pressed == false) {
                sprites[0].object_dir = new_object_dir;
                sprites[0].prg_movetype = pmmNone;
                joy_pressed = true;
            }
        } else if ((new_object_dir != sprites[0].object_dir) || (sprites[0].prg_movetype == pmmMoveTo)) {
            sprites[0].object_dir = new_object_dir;
            sprites[0].prg_movetype = pmmNone;
            joy_pressed = true;
        } else if (joy_pressed == false) {
            sprites[0].object_dir = 0;
            joy_pressed = true;
        }
    } else {
        joy_pressed = false;
    }
}

void handle_movement_mouse(void) {
    if (mouse_leftclick == 1) {
        if (mouse_down == false) {
            if (mouse_ypos > 8) {
                if (!player_control) {
                    return;
                }

                sprites[0].prg_movetype = pmmMoveTo;
                sprites[0].prg_x_destination = (mouse_xpos >> 1) - (sprites[0].view_info.width >> 1);
                sprites[0].prg_y_destination = mouse_ypos - 8;
                sprites[0].prg_speed = sprites[0].step_size;
                sprites[0].prg_distance = sprites[0].prg_speed * sprites[0].prg_speed;
                sprites[0].prg_complete_flag = 0;
            } else {
                dialog_draw_menubar(true);
            } 
        }
        mouse_down = true;
    } else {
        mouse_down = false;
    }
}

void engine_statusline(bool enable) {
    status_line_enabled = enable;
    if (!enable) {
        textscr_set_color(COLOR_WHITE, COLOR_BLACK);
        textscr_print_ascii(0, 0, (uint8_t *)"%p40");
    }
}

void engine_clear_keyboard(void) {
    dialog_clear_keyboard();
}

void engine_allowinput(bool allowed) {
    input_ok = allowed;
    if (input_ok) {
        engine_clear_keyboard();
    } else {
        textscr_set_color(COLOR_WHITE, COLOR_BLACK);
        textscr_print_ascii(0, 22, (uint8_t *)"%p40");
    }
}

void run_loop(void) {
    frame_counter = 0;
    run_cycles = 0;
    run_engine = false;
    engine_running = false;
    sound_flag_needs_set = false;
    status_line_enabled = false;
    status_line_score = 255;
    status_line_sound = true;
    quit_flag = false;

    textscr_init();
    dialog_init();

    select_engine_enginehigh_mem();
    engine_show_welcome_text();

    hook_irq();
    view_init();
    sprite_init();
    logic_init();
    parser_init();
    mouse_init();
    input_ok = false;
    player_control = true;
    logic_set_flag(9);
    logic_set_flag(11);
    logic_vars[20] = 128;
    logic_vars[22] = 3;
    logic_vars[23] = 0x0f;
    logic_vars[24] = 37;
    logic_vars[25] = 3;
    logic_set_flag(5);
    gfx_cleargfx(false);
    while (!quit_flag) {
        while(!run_engine);
        engine_running = true;
        run_engine = false;
        game_timeslot_ready = false;

        run_cycles++;
        select_engine_enginehigh_mem();
        if (!dialog_proc()) {
            if (run_cycles >= logic_vars[10]) {
                if (sound_flag_needs_set) {
                    sound_flag_needs_set = false;
                    logic_set_flag(sound_flag_end);
                }
                engine_update_status_line(false);
                sprite_undraw();
                joyports_poll();
                handle_movement_joystick();
                handle_movement_mouse();
                clocksync();
                logic_run();
                logic_reset_flag(11);
                sprite_updateanddraw();
                run_cycles = 0;
                if (logic_flag_isset(2) || logic_flag_isset(4) || (logic_vars[9] > 0)) {
                    // Parser did something
                    logic_vars[9] = 0;
                    if (input_ok) {
                        dialog_clear_keyboard();
                    }
                }
                logic_reset_flag(2);
                logic_reset_flag(4);
                logic_reset_all_controllers();
            }
        }
        engine_running = false;
    }
    __asm (
        " lda #0x7e\n"
        " sta 0xd640\n"
        " clv"
    );
}

#pragma clang section bss="" data="" rodata="" text=""

__attribute__((interrupt))
void engine_interrupt_handler(void) {
    volatile uint8_t *irr = (uint8_t *)0xd019;
    if (!((*irr) & 0x01)) {
        // not a raster interrupt, ignore
        return;
    }

    mouse_irq();
    sound_interrupt_handler();
    
    frame_counter++;
    if (frame_counter >= 3) {
        if (!engine_running) {
            gfx_flippage();
            run_engine = true;
        }
        game_timeslot_ready = true;
        frame_counter = 0;
    }
    *irr = *irr; // ack interrupt
}
