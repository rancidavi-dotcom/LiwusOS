CC = gcc
LD = ld
SDK_DIR ?= ../../build/liwus-sdk
CFLAGS = -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
	-I$(SDK_DIR)/include
LXE = $(SDK_DIR)/bin/ls.lxe

.PHONY: all clean
all: $(LXE)

$(LXE): main.c $(SDK_DIR)/lib/libliwus.a $(SDK_DIR)/lib/crt0.o \
		$(SDK_DIR)/share/liwus/elf64.ld
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c main.c -o $(SDK_DIR)/ls-main.o
	$(LD) -m elf_x86_64 -nostdlib -static --build-id=none \
		-T $(SDK_DIR)/share/liwus/elf64.ld -o $@ \
		$(SDK_DIR)/lib/crt0.o $(SDK_DIR)/ls-main.o $(SDK_DIR)/lib/libliwus.a

clean:
	rm -f $(SDK_DIR)/ls-main.o $(LXE)
