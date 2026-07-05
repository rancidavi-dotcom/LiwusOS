CC = gcc
AR = ar

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I../sdk/include -fno-pie -fno-pic -mno-red-zone

all: liblgx.a

liblgx.a: lgx.o
	$(AR) rcs $@ $^
	cp liblgx.a ../sdk/lib/

lgx.o: lgx.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.a
