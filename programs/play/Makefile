CC = gcc
LD = ld
SDK_DIR ?= ../../build/liwus-sdk
CFLAGS = -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
	-I$(SDK_DIR)/include -I../../include -I../../third_party/minimp3
LXE = $(SDK_DIR)/bin/play.lxe

.PHONY: all clean
all: $(LXE)

$(LXE): main.c ../../src/kernel/core/media.c ../../include/kernel/media.h \
		../../third_party/minimp3/minimp3.h $(SDK_DIR)/lib/libliwus.a \
		$(SDK_DIR)/lib/crt0.o $(SDK_DIR)/share/liwus/elf64.ld
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c main.c -o $(SDK_DIR)/play-main.o
	$(CC) $(CFLAGS) -c ../../src/kernel/core/media.c -o $(SDK_DIR)/play-media.o
	$(LD) -m elf_x86_64 -nostdlib -static --build-id=none \
		-T $(SDK_DIR)/share/liwus/elf64.ld -o $@ \
		$(SDK_DIR)/lib/crt0.o $(SDK_DIR)/play-main.o $(SDK_DIR)/play-media.o \
		$(SDK_DIR)/lib/libliwus.a

clean:
	rm -f $(SDK_DIR)/play-main.o $(SDK_DIR)/play-media.o $(LXE)
