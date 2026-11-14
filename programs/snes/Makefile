CC ?= gcc
LD ?= ld
SDK_DIR ?= ../../build/liwus-sdk
OBJ_DIR ?= $(SDK_DIR)/snes-obj

CFLAGS = -std=gnu99 -O2 -ffreestanding -fno-builtin \
	-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
	-w -I../../third_party/lakesnes/snes -I$(SDK_DIR)/include

LXE = $(SDK_DIR)/bin/snes.lxe
CORE_DIR = ../../third_party/lakesnes/snes
CORE_SRCS = spc.c dsp.c apu.c cpu.c dma.c ppu.c cart.c cx4.c input.c \
	statehandler.c snes.c snes_other.c
CORE_OBJS = $(addprefix $(OBJ_DIR)/core/,$(CORE_SRCS:.c=.o))
APP_OBJ = $(OBJ_DIR)/snes-main.o

.PHONY: all clean
all: $(LXE)

$(OBJ_DIR)/core/%.o: $(CORE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(APP_OBJ): main.c ../../third_party/lakesnes/snes/snes.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LXE): $(APP_OBJ) $(CORE_OBJS) $(SDK_DIR)/lib/libliwus.a \
		$(SDK_DIR)/lib/crt0.o $(SDK_DIR)/share/liwus/elf64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -nostdlib -static --build-id=none \
		-T $(SDK_DIR)/share/liwus/elf64.ld -o $@ \
		$(SDK_DIR)/lib/crt0.o $(APP_OBJ) $(CORE_OBJS) \
		$(SDK_DIR)/lib/libliwus.a $(SDK_DIR)/lib/libopenlibm.a

clean:
	rm -rf $(OBJ_DIR) $(LXE)
