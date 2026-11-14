CC ?= gcc
LD ?= ld
SDK_DIR ?= ../../build/liwus-sdk
CFLAGS = -std=gnu99 -O2 -ffreestanding -fno-builtin \
	-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
	-I$(SDK_DIR)/include -I. -DONE_SOURCE=1 -DTCC_TARGET_X86_64=1

LXE = $(SDK_DIR)/bin/tcc.lxe

.PHONY: all clean

all: $(LXE)

$(LXE): tcc.c tccdefs_.h $(SDK_DIR)/lib/libliwus.a $(SDK_DIR)/lib/crt0.o $(SDK_DIR)/share/liwus/elf64.ld
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c tcc.c -o $(SDK_DIR)/tcc-main.o
	$(LD) -m elf_x86_64 -nostdlib -static --build-id=none \
		-T $(SDK_DIR)/share/liwus/elf64.ld -o $@ \
		$(SDK_DIR)/lib/crt0.o $(SDK_DIR)/tcc-main.o \
		$(SDK_DIR)/lib/libliwus.a

clean:
	rm -f $(SDK_DIR)/tcc-main.o $(LXE)
