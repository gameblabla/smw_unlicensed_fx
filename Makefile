PREFIX ?= v810
V810GCC ?= /opt/v810-gcc
CC := $(V810GCC)/bin/$(PREFIX)-gcc
LD := $(V810GCC)/bin/$(PREFIX)-ld
OBJCOPY := $(V810GCC)/bin/$(PREFIX)-objcopy
BINCUT := $(V810GCC)/bin/bincat
PCFX_CDLINK := $(V810GCC)/bin/pcfx-cdlink
TARGET := smb_pcfx
BOSSREF ?= assets/reference/boss
LEVEL_OVERRIDES ?= level_overrides
LEVEL_DEFAULTS ?= level_defaults
OBJDIR := build
INCLUDES := -I. -Isrc -I$(V810GCC)/include -I$(V810GCC)/$(PREFIX)/include
CFLAGS := -O2 -fomit-frame-pointer -fno-builtin -ffunction-sections -fdata-sections \
          -Wall -Wextra -std=gnu99 -mv810 -mprolog-function -msda=0 $(INCLUDES)
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS := -L$(V810GCC)/lib -L$(V810GCC)/$(PREFIX)/lib \
           -L$(V810GCC)/lib/gcc/$(PREFIX)/4.9.4 \
           $(V810GCC)/$(PREFIX)/lib/crt0.o --gc-sections
LIBS := -lpcfx -lc -lsim -lgcc
SRCS := src/main.c src/assets_generated.c src/boot_exact.c src/boot_dual.c src/audio_stub.c src/music.c src/game_levels_generated.c src/staff_roll.c
ASM_SRCS := src/pcfx_tetsu_wait.S
OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRCS)) $(patsubst src/%.S,$(OBJDIR)/%.o,$(ASM_SRCS))
ELF := $(OBJDIR)/$(TARGET).elf
PROGRAM_BIN := $(OBJDIR)/$(TARGET).bin
MAP := $(OBJDIR)/$(TARGET).map

.PHONY: all disc clean assets editor apply-level-edits vram-map
CDDA_TOOL := tools/build_cdda.py
CDDA_WAVS := NEWMUSIC/BOSS.wav NEWMUSIC/continue.wav NEWMUSIC/forest.wav \
             NEWMUSIC/graveyard.wav NEWMUSIC/nuclear\ dump.wav NEWMUSIC/staff.wav \
             NEWMUSIC/street.wav NEWMUSIC/titlescreen.wav NEWMUSIC/world.wav
SFX_ADPCM_HEADER := src/sfx_adpcm_generated.h
SFX_WAVS := megadrive_sfx_wav_lowlevel/command_90.wav \
            megadrive_sfx_wav_lowlevel/command_9C.wav \
            megadrive_sfx_wav_lowlevel/command_99.wav \
            megadrive_sfx_wav_lowlevel/command_9D.wav \
            megadrive_sfx_wav_lowlevel/command_9E.wav \
            megadrive_sfx_wav_lowlevel/command_AE.wav \
            megadrive_sfx_wav_lowlevel/command_A8.wav \
            megadrive_sfx_wav_lowlevel/command_A9.wav
all: disc
assets:
	KIT=$(KIT) python3 tools/prepare_boot_frames.py
	python3 tools/build_assets.py --kit $(KIT) --rom $(ROM) --project .
	python3 tools/build_boot_exact.py --frames assets/derived/boot_exact_fixed --project . --first 12 --last 600
	python3 tools/build_boot_dual.py
	KIT=$(KIT) ROM=$(ROM) BOSSREF=$(BOSSREF) LEVEL_DEFAULTS=$(LEVEL_DEFAULTS) LEVEL_OVERRIDES=$(LEVEL_OVERRIDES) python3 tools/build_full_game.py
vram-map:
	python3 tools/vram_map.py --report
editor:
	python3 tools/level_editor.py --kit $(if $(KIT),$(KIT),Super_Mario_Bros_MD_reconstruction_kit) --defaults $(LEVEL_DEFAULTS) --overrides $(LEVEL_OVERRIDES)
apply-level-edits:
	KIT=$(KIT) ROM=$(ROM) BOSSREF=$(BOSSREF) LEVEL_DEFAULTS=$(LEVEL_DEFAULTS) LEVEL_OVERRIDES=$(LEVEL_OVERRIDES) python3 tools/build_full_game.py
$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
VRAM_MAP_HEADER := src/vram_map.h
# Regenerating the header re-runs the overlap check, so every build validates
# the VRAM layout against the current generated assets.
$(VRAM_MAP_HEADER): tools/vram_map.py src/game_levels_generated.c src/assets_generated.c
	python3 tools/vram_map.py
STAFF_FONT_HEADER := src/staff_font_generated.h
$(STAFF_FONT_HEADER): tools/build_staff_font.py
	python3 tools/build_staff_font.py
$(OBJDIR)/main.o: $(VRAM_MAP_HEADER) $(STAFF_FONT_HEADER)
$(OBJDIR)/audio_stub.o: $(SFX_ADPCM_HEADER)
$(SFX_ADPCM_HEADER): tools/build_sfx_adpcm.py $(SFX_WAVS)
	python3 tools/build_sfx_adpcm.py
$(OBJDIR)/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@ -Map $(MAP)
$(PROGRAM_BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
out_ram.bin: $(PROGRAM_BIN)
	$(BINCUT) out_ram.bin $(OBJDIR)/lbas_ram.h $(PROGRAM_BIN)
disc: out_ram.bin cdlink.txt $(CDDA_TOOL) $(CDDA_WAVS)
	$(PCFX_CDLINK) cdlink.txt $(TARGET)
	python3 $(CDDA_TOOL) --music-dir NEWMUSIC --output $(TARGET) --cue $(TARGET).cue --header src/cdda_tracks.h
clean:
	rm -rf $(OBJDIR) out_ram.bin $(TARGET).cue $(TARGET).bin $(TARGET)_*.bin
