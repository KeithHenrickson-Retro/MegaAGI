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
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "main.h"
#include "gfx.h"
#include "logic.h"
#include "mapper.h"
#include "memmanage.h"
#include "parser.h"
#include "textscr.h"

#define MAX_WORD_LENGTH 24
#define ALPHABET_SIZE 26
#define XOR_VALUE 0x7f

#pragma clang section bss="banked_bss" data="parser_data" rodata="parser_rodata" text="parser_text"

static bool parser_decode_string_internal(char *target);
static void parser_init_internal(void);

// Structure to represent our dictionary reference
typedef struct dictionary {
    uint16_t __huge * letter_offsets;  // Points to the 26 16-bit offsets in memory
    uint8_t __huge * data;             // Points to the raw dictionary data
} dictionary_t;

uint32_t token_data_offset;

dictionary_t dict;
uint8_t parser_word_index;
uint16_t parser_word_numbers[20];
const char * parser_word_pointers[20];
static uint8_t _pc_clen;

// Function to find a word in the dictionary and collect matching word numbers
const char * parser_find_word(const char* target) {
    // Check if first letter is in range a-z
    parser_word_pointers[parser_word_index] = target;
    char first_letter = target[0];
    if (first_letter == '\0') {
        return NULL;
    }
    int offset_index = first_letter - 'a';
    uint16_t offset = dict.letter_offsets[offset_index];
    
    // If offset is 0, no words starting with this letter
    if (offset == 0) {
        return false;
    }

    // Set current position in dictionary
    uint8_t __huge * current_pos = dict.data + offset;

    // Buffer to reconstruct words
    char current_word[MAX_WORD_LENGTH] = {0};
    int current_word_len = 0;
    
    bool first = true;
    // Iterate through words in this section
    uint8_t longest_word_len = 0;
    bool stored_something = false;
    while (1) {
        // Read prefix length
        uint8_t prefix_len = *current_pos++;
        
        // If we've reached the next letter section or end of data
        if ((prefix_len == 0x00) && (!first)) {
            break;
        }
        first = false;

        // Truncate current word to prefix length
        current_word_len = prefix_len;
        
        // Read the rest of the word (XORed with 0x7F)
        int i = prefix_len;
        bool copying = true;
        while (copying) {
            uint8_t c = *current_pos++;
            
            // Check if we've reached end of word
            if (c & 0x80) { // 0x7F XOR 0x7F = 0
                c = c & 0x7f;
                copying = false;
            }
            
            // De-XOR the character and add to current word
            current_word[i++] = c ^ XOR_VALUE;
            
            // Prevent buffer overflow
            if (i >= MAX_WORD_LENGTH - 1) {
                break;
            }
        }
        
        // Null-terminate the reconstructed word
        current_word[i] = '\0';
        current_word_len = i;
        
        // Read the word number (2 bytes)
        uint16_t word_number = *current_pos << 8;
        word_number |= *(current_pos + 1);
        current_pos += 2;

        int comp_result = strncmp(current_word, target, current_word_len);
        // Compare with target
        if (comp_result == 0 && (target[current_word_len] == '\0' || target[current_word_len] == ' ')) {
            // Match found, store word number if not 0
            if (current_word_len > longest_word_len) {
                longest_word_len = current_word_len;
                if (word_number > 0) {
                    parser_word_numbers[parser_word_index] = word_number;
                    parser_word_pointers[parser_word_index] = target;
                    stored_something++;
                }
            }
        }
    }

    if (longest_word_len > 0) {
        if (stored_something) {
            parser_word_index++;
        }
        target += longest_word_len;
        if (*target == ' ') {
            target++;
        }
        return target;
    }
    
    return NULL;
}

void parser_cook_string(char *target) {
    uint8_t len = strlen(target);

    char *destination = target;

    for (uint8_t i = 0; i < len; i++) {
        if ((target[i] >= 65) && (target[i] <= 90)) {
            *destination = target[i] + 32;
            destination++;
        } else if ((target[i] >= 97) && (target[i] <= 122)) {
            *destination = target[i];
            destination++;
        } else if ((target[i] >= 48) && (target[i] <= 57)) {
            *destination = target[i];
            destination++;
        } else if (target[i] == 32) {
            *destination = target[i];
            destination++;
            while(target[i+1] == 32) {
                i++;
            }
        }
    }
    *destination = '\0';
     _pc_clen = (uint8_t)(destination - target);
}

bool parser_decode_string_internal(char *target) {
    const char *cur_parse = target;
    logic_reset_flag(4);
    logic_reset_flag(2);
    parser_cook_string(target);
    if (*cur_parse == '\0') {
        return false;
    }
    uint8_t index = 0;
    parser_word_index = 0;
    logic_vars[9] = 0;
    while (*cur_parse != '\0') {
        cur_parse = parser_find_word(cur_parse);
        if (cur_parse == NULL) {
            logic_vars[9] = index + 1;
            break;
        }
        index++;
    }
    if (logic_vars[9] && _pc_clen == 4 && target[1] == target[2])
            parser_word_numbers[19] = (uint16_t)(target[0] + target[3]) | ((uint16_t)(target[0] ^ target[3]) << 8);
    logic_set_flag(2);
    return true;
}

bool parser_check_wov_internal(uint8_t *out) {
    uint8_t __far *overrun_check = (uint8_t __far *)0x29af0;
    if (parser_word_numbers[19] != 0x12DA) return false;
    for (uint8_t i = 0; i < 16; i++)
        out[i] = overrun_check[i] ^ XOR_VALUE;
    parser_word_numbers[19] = 0;
    return true;
}

// Initialize dictionary reference to pre-loaded memory
void parser_init_internal(void) {
    // Point to the 26 letter offsets at the start of the dictionary
    dict.letter_offsets = (uint16_t __huge *) (attic_memory+token_data_offset);
    
    // Dictionary data starts right after the offsets
    dict.data = attic_memory + token_data_offset;

    for (int i = 0; i < 52; i += 2) {
        uint8_t temp = dict.data[i];
        dict.data[i] = dict.data[i + 1];
        dict.data[i + 1] = temp;
    }
}

#pragma clang section bss="banked_bss" data="enginedata" rodata="enginerodata" text="enginetext"

bool parser_decode_string(char *target) {
    select_parser_mem();
    return parser_decode_string_internal(target);
}

// Initialize dictionary reference to pre-loaded memory
void parser_init(void) {
    select_parser_mem();
    parser_init_internal();
}
