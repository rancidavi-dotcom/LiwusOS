CC = gcc
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -I../../include
AR = ar

all: libliwus.a crt0.o

libliwus.a: syscall.o crt.o string.o
	rm -f $@
	$(AR) rcs $@ $^

syscall.o: syscall.c $(wildcard ../../include/liwus/*.h) ../../include/uapi/syscall_nums.h
	$(CC) $(CFLAGS) -c $< -o $@

crt0.o: crt0.S
	$(CC) -m64 -fno-pie -fno-pic -c $< -o $@

crt.o: crt.c ../../include/liwus/syscall.h
	$(CC) $(CFLAGS) -c $< -o $@

string.o: string.c ../../include/liwus/string.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o libliwus.a

.PHONY: all clean
