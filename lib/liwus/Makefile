CC = gcc
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -I../../include -I../c_example

all: syscall.o crt0.o crt.o

syscall.o: syscall.c
	$(CC) $(CFLAGS) -c $< -o $@

crt0.o: crt0.S
	$(CC) -m64 -fno-pie -fno-pic -c $< -o $@

crt.o: crt.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o

.PHONY: all clean
