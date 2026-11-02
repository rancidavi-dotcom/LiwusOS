CC = gcc
LD = ld
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -I../../include -I../c_example
LDFLAGS = -nostdlib -static -no-pie 

all: native-hello.elf

native-hello.elf: main.o ../../lib/liwus/syscall.o ../../lib/liwus/crt0.o ../../lib/liwus/crt.o linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ ../../lib/liwus/crt0.o ../../lib/liwus/crt.o ../../lib/liwus/syscall.o main.o

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

../../lib/liwus/syscall.o:
	$(MAKE) -C ../../lib/liwus syscall.o 2>/dev/null || \
