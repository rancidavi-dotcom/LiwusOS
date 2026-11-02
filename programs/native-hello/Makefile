CC = gcc
LD = ld
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Werror \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -I../../include
LDFLAGS = -nostdlib -static -no-pie
LXE = ../../build/liwus-sdk/bin/native-hello.lxe

.PHONY: all lxe sdk-lib legacy-elf clean
all: $(LXE)
lxe: $(LXE)
legacy-elf: native-hello.elf

sdk-lib:
	$(MAKE) -C ../../lib/liwus all

native-hello.elf: main.o sdk-lib linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ ../../lib/liwus/crt0.o \
		../../lib/liwus/crt.o ../../lib/liwus/syscall.o main.o

$(LXE): main.o sdk-lib ../../sdk/elf64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -nostdlib -static --build-id=none -T ../../sdk/elf64.ld -o $@ \
		../../lib/liwus/crt0.o main.o ../../lib/liwus/libliwus.a

main.o: main.c $(wildcard ../../include/liwus/*.h)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f main.o native-hello.elf $(LXE)
