CC = gcc
LD = ld
SDK_DIR ?= ../../build/liwus-sdk
CFLAGS = -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
	-I$(SDK_DIR)/include -I../../include
LXE = $(SDK_DIR)/bin/tube.lxe

.PHONY: all clean
all: $(LXE)

$(LXE): main.c ../../src/net/liwustube.c ../../include/net/liwustube.h \
		$(SDK_DIR)/lib/libliwus.a $(SDK_DIR)/lib/crt0.o $(SDK_DIR)/share/liwus/elf64.ld
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c main.c -o $(SDK_DIR)/tube-main.o
	$(CC) $(CFLAGS) -c ../../src/net/liwustube.c -o $(SDK_DIR)/tube-lib.o
	$(LD) -m elf_x86_64 -nostdlib -static --build-id=none \
		-T $(SDK_DIR)/share/liwus/elf64.ld -o $@ \
		$(SDK_DIR)/lib/crt0.o $(SDK_DIR)/tube-main.o $(SDK_DIR)/tube-lib.o \
		$(SDK_DIR)/lib/libliwus.a

clean:
	rm -f $(SDK_DIR)/tube-main.o $(SDK_DIR)/tube-lib.o $(LXE)
