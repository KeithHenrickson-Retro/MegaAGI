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
#include <string.h>
#include <mega65.h>

#include "sound.h"
#include "irq.h"
#include "logic.h"
#include "main.h"
#include "volume.h"

volatile uint8_t __huge *sound_file;
volatile uint16_t voice_offsets[3] = {0};
volatile uint8_t voice_stopped[3] = {1};
volatile uint16_t durations[3] = {0};
volatile uint8_t voice_holds[3];
volatile uint8_t sound_flag_end;
volatile uint8_t sound_running;
volatile bool request_stop;
volatile bool sound_flag_needs_set;
volatile uint8_t next_sound_flag_end;
volatile uint8_t __huge * next_sound_file;
volatile bool sound_queued;

#define OPL2REG (*(volatile uint8_t __far *)0x7ffdf40)
#define OPL2VAL (*(volatile uint8_t __far *)0x7ffdf50)
#define MIXERREG (*(volatile uint8_t *)0xd6f4)
#define MIXERVAL (*(volatile uint8_t *)0xd6f5)

typedef struct {
    uint8_t reg_lo;
    uint8_t reg_hi;
 } noteval_t;

typedef struct {
    uint32_t numerator;
    uint8_t reg_hi;
 } noteconvert_t;

#pragma clang section bss="extradata"
__far static noteconvert_t note_numerators[8];
#pragma clang section bss=""

#pragma clang section bss = "banked_bss" data = "initsdata" rodata = "initsrodata" text = "initstext"

static const noteconvert_t note_numerators_init[8] = {
    {0, 0}, {1179646, 36}, {589823, 40}, {294911, 44}, {147456, 48}, {73728, 52}, {36864, 56}, {18432, 60}
};

void sound_init_opl_op(uint8_t operator_offset, uint8_t flags, uint8_t scaling_level, uint8_t attack_decay, uint8_t sustain_release) {
    OPL2REG = 0x20 + operator_offset;
    OPL2VAL = flags;
    OPL2REG = 0x40 + operator_offset;
    OPL2VAL = scaling_level;
    OPL2REG = 0x60 + operator_offset;
    OPL2VAL = attack_decay;
    OPL2REG = 0x80 + operator_offset;
    OPL2VAL = sustain_release;
}

void sound_init_opl(void) {
    // init modulators
    sound_init_opl_op(0, 0x23, 0xc8, 0xf0, 0x01);
    sound_init_opl_op(1, 0x23, 0xc8, 0xf0, 0x01);
    sound_init_opl_op(2, 0x23, 0xc8, 0xf0, 0x01);

    // init carriers
    sound_init_opl_op(3, 0x21, 0x0b, 0xe0, 0x86);
    sound_init_opl_op(4, 0x21, 0x0b, 0xe0, 0x86);
    sound_init_opl_op(5, 0x21, 0x0b, 0xe0, 0x86);

    // init feedback
    for (uint8_t i = 0; i < 8; i++) {
        OPL2REG = 0xC0 + i;
        OPL2VAL = 0xF2;
    }

    // clear music
    for (uint8_t i = 0; i < 8; i++) {
        OPL2REG = 0xB0 + i;
        OPL2VAL = 0x00;
    }

    // init mixer
    uint8_t mixer_data[] = {0x1c, 0x1d, 0x3c, 0x3d, 0xdc, 0xdd, 0xfc, 0xfd};
    for (uint8_t i = 0; i < sizeof(mixer_data); i++) {
        MIXERREG = mixer_data[i];
        MIXERVAL = 0xc0;
    }

    for (uint8_t i = 0; i < sizeof(note_numerators); i++) {
        note_numerators[i] = note_numerators_init[i];
    }
}

#pragma clang section bss="banked_bss" data="ls_spritedata" rodata="ls_spriterodata" text="ls_spritetext"

void sound_play(uint8_t sound_num, uint8_t flag_at_end) {
    uint16_t length;
    if (sound_running) {
        sound_stop();
    }

    next_sound_file = volume_locate_object(voSound, sound_num, &length);
    if (next_sound_file == NULL) {
        return;
    }

    next_sound_flag_end = flag_at_end;
    sound_queued = true;
}

void sound_stop(void) {
    if (sound_running) {
        request_stop = true;
    }
}


#pragma clang section bss="banked_bss" data="data" rodata="data" text="code"

static void sound_play_queued(void) {
    sound_queued = false;
    sound_file = next_sound_file;
    sound_flag_end = next_sound_flag_end;
    logic_reset_flag(sound_flag_end);

    voice_offsets[0] = (sound_file[1] << 8) | sound_file[0];
    voice_offsets[1] = (sound_file[3] << 8) | sound_file[2];
    voice_offsets[2] = (sound_file[5] << 8) | sound_file[4];

    SID1.amp = 0x00;
    
    sound_running = 1;
    request_stop = false;

    durations[0] = 1;
    durations[1] = 1;
    durations[2] = 1;
    voice_stopped[0] = 0;
    voice_stopped[1] = 0;
    voice_stopped[2] = 0;
}

void sound_interrupt_handler(void) {
    uint8_t acted = 0;
    for (uint8_t voice=0; voice < 3; voice++) {
        --durations[voice];
        if (!voice_stopped[voice] && logic_flag_isset(9)) {
            acted = 1;
            uint16_t voice_offset = voice_offsets[voice];
            
            if (durations[voice] < voice_holds[voice]) {
                OPL2REG = 0xB0 + voice;
                OPL2VAL = 0x00;
            }
            if (durations[voice] == 0) {
                uint16_t duration = (sound_file[voice_offset+1] << 8);
                duration = duration + sound_file[voice_offset];
                if (duration == 0xffff) {
                    OPL2REG = 0xB0 + voice;
                    OPL2VAL = 0x00;
                    voice_stopped[voice] = 1;
                    continue;
                }
                uint16_t tandyfnum = (((sound_file[voice_offset + 2] & 0x3f) << 4) + (sound_file[voice_offset + 3] & 0x0f));
                uint16_t vol = sound_file[voice_offset + 4];
                voice_offsets[voice] = voice_offsets[voice] + 5;
                if ((vol & 0x0f) == 0x0f) {
                    OPL2REG = 0xB0 + voice;
                    OPL2VAL = 0x00;
                } else {
                    uint8_t oplblock;
                    uint16_t oplfnum = 0x00;
                    oplblock = (tandyfnum >= 577) ? 2 :
                            (tandyfnum >= 289) ? 3 :
                            (tandyfnum >= 145) ? 4 :
                            (tandyfnum >=  73) ? 5 :
                            (tandyfnum >=  37) ? 6 : 7;

                    oplfnum = note_numerators[oplblock].numerator / tandyfnum;
                    OPL2REG = 0xA0 + voice;
                    OPL2VAL = oplfnum & 0xff;
                    OPL2REG = 0xB0 + voice;
                    OPL2VAL = note_numerators[oplblock].reg_hi | (oplfnum >> 8);
                }
                durations[voice] = duration;
                if (duration < 6) {
                    voice_holds[voice] = 0;
                } else {
                    voice_holds[voice] = 3;
                }
            }
        }
    }
    if (request_stop) {
        voice_stopped[0] = 1;
        voice_stopped[1] = 1;
        voice_stopped[2] = 1;
    }
    if (sound_running && !acted) {
        OPL2REG = 0xB0;
        OPL2VAL = 0x00;
        OPL2REG = 0xB1;
        OPL2VAL = 0x00;
        OPL2REG = 0xB2;
        OPL2VAL = 0x00;
        SID1.amp = 0x00;
        sound_flag_needs_set = true;
        sound_running = 0;
    }

    if (!sound_running && sound_queued) {
        sound_play_queued();
    }
}