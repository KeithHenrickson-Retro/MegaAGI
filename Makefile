VPATH = src
ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

# Common source files
ASM_SRCS = clock.s diskasm.s irq.s startup.s mapper.s mouse.s ports.s
C_SRCS = main.c ncm.c pic.c volume.c sound.c view.c engine.c memmanage.c sprite.c logic.c parser.c init.c gamesave.c dialog.c disk.c textscr.c
C1541 = flatpak run --command=c1541 net.sf.VICE
INC = -I./include

# Object files
OBJS = $(ASM_SRCS:%.s=obj/%.o) $(C_SRCS:%.c=obj/%.o)
OBJS_DEBUG = $(ASM_SRCS:%.s=obj/%-debug.o) $(C_SRCS:%.c=obj/%-debug.o)

# GIT repository information
ifneq "$(wildcard $(ROOT_DIR)/.git )" "" #check if local repo exist
#Create variable GIT_MSG with:
#1) git sha and additionally dirty flag
GIT_MSG := sha:$(shell git --git-dir=$(ROOT_DIR)/.git --work-tree=$(ROOT_DIR) --no-pager describe --tags --always --dirty)
else
GIT_MSG := Can't find git repo
endif
#add user define macro (-D) to gcc
CFLAGS += -DGIT_MSG=\"$(strip "$(GIT_MSG)")\"
CFLAGS += --no-cross-call --strong-inline --inline-on-matching-custom-text-section --no-interprocedural-cross-jump

obj/%.o: %.s
	as6502 --target=mega65 --list-file=$(@:%.o=%.clst) -o $@ $<

obj/%.o: %.c
	cc6502 --target=mega65 -Wall -Werror -O2 $(INC) --list-file=$(@:%.o=%.clst) $(CFLAGS) -o $@ $<

obj/%-debug.o: %.s
	as6502 --target=mega65 --debug --list-file=$(@:%.o=%.clst) -o $@ $<

obj/%-debug.o: %.c
	cc6502 --target=mega65 --debug --list-file=$(@:%.o=%.clst) -o $@ $<

agi.prg:  mega65-agi.scm $(OBJS)
	ln6502 --verbose --target=mega65 -o $@ $^ --raw-multiple-memories --output-format=prg --cstartup=megaagi --rtattr exit=simplified --list-file=agi-mega65.cmap

agi.elf: $(OBJS_DEBUG)
	ln6502 --target=mega65 --debug -o $@ $^ --list-file=agi-debug.cmap --semi-hosted

agi.exo: agi.prg
	exomizer sfx basic -n -t 65 -o agi.exo agi.prg

agi.lgo: agi.exo
	./build-logo.sh

mega65-agi.d81: agi.lgo
	$(C1541) -format "mega65,agi" d81 mega65-agi.d81
	$(C1541) -attach mega65-agi.d81 -write agi.lgo agi.c65
	$(C1541) -attach mega65-agi.d81 -write COPYING copying,s
	$(C1541) -attach mega65-agi.d81 -write gamecode.raw gamecode,s
	
agi.d81: agi.lgo
	$(C1541) -format "agi,a1" d81 agi.d81
	$(C1541) -attach agi.d81 -write agi.lgo agi.c65
	$(C1541) -attach agi.d81 -write COPYING copying,s
	$(C1541) -attach agi.d81 -write gamecode.raw gamecode,s
	$(C1541) -attach agi.d81 -write xmas/LOGDIR logdir,s
	$(C1541) -attach agi.d81 -write xmas/PICDIR picdir,s
	$(C1541) -attach agi.d81 -write xmas/SNDDIR snddir,s
	$(C1541) -attach agi.d81 -write xmas/VIEWDIR viewdir,s
	$(C1541) -attach agi.d81 -write xmas/VOL.0 vol.0,s
	$(C1541) -attach agi.d81 -write xmas/WORDS.TOK words.tok,s
	$(C1541) -attach agi.d81 -write xmas/OBJECT object,s

agisystem: agi.d81 mega65-agi.d81

clean:
	-rm $(OBJS) $(OBJS:%.o=%.clst) $(OBJS_DEBUG) $(OBJS_DEBUG:%.o=%.clst)
	-rm agi.elf agi.prg agi-mega65.cmap agi-debug.cmap
