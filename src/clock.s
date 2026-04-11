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

            .extern _Zp

TODJIF:     .equlab 0xdc08
TODSEC:     .equlab 0xdc09
TODMIN:     .equlab 0xdc0a
TODHR:      .equlab 0xdc0b

            .section enginetext

            .public clocksync
clocksync:  
            lda #0x0c
            ldx #0x00
            ldy #0x04
            ldz #0x00
            stq zp:_Zp+4

            ldx #0x03
            ldz #0x02
-:          lda TODJIF,x
            sta [zp:_Zp+4],z
            dez
            dex
            bne -
            lda TODJIF
            rts

            .public clockpause
clockpause:
            lda TODHR
            sta TODHR
            rts

            .public clockresume
clockresume:
            lda TODJIF
            sta TODJIF
            rts