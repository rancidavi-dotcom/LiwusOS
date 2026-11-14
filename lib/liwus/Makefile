CC = gcc
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -I../../include
AR = ar

OBJS = syscall.o crt.o string.o malloc.o stdio.o printf.o scanf.o strtol.o setjmp.o posix.o time.o math.o env.o qsort.o signal.o webdom.o

all: libliwus.a crt0.o

libliwus.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

syscall.o: syscall.c $(wildcard ../../include/liwus/*.h) ../../include/uapi/syscall_nums.h
	$(CC) $(CFLAGS) -c $< -o $@

crt0.o: crt0.S
	$(CC) -m64 -fno-pie -fno-pic -c $< -o $@

crt.o: crt.c ../../include/liwus/syscall.h
	$(CC) $(CFLAGS) -c $< -o $@

string.o: string.c ../../include/liwus/string.h ../../include/liwus/stdlib.h
	$(CC) $(CFLAGS) -c $< -o $@

malloc.o: malloc.c ../../include/liwus/stdlib.h ../../include/liwus/mman.h
	$(CC) $(CFLAGS) -c $< -o $@

stdio.o: stdio.c ../../include/liwus/stdio.h ../../include/liwus/stdlib.h
	$(CC) $(CFLAGS) -c $< -o $@

printf.o: printf.c ../../include/liwus/stdio.h ../../include/liwus/string.h
	$(CC) $(CFLAGS) -c $< -o $@

scanf.o: scanf.c ../../include/liwus/stdio.h ../../include/liwus/string.h
	$(CC) $(CFLAGS) -c $< -o $@

strtol.o: strtol.c ../../include/liwus/stdlib.h
	$(CC) $(CFLAGS) -c $< -o $@

setjmp.o: setjmp.S
	$(CC) -m64 -fno-pie -fno-pic -c $< -o $@

posix.o: posix.c ../../include/liwus/unistd.h ../../include/liwus/syscall.h
	$(CC) $(CFLAGS) -c $< -o $@

time.o: time.c ../../include/liwus/time.h ../../include/liwus/syscall.h
	$(CC) $(CFLAGS) -c $< -o $@

math.o: math.c ../../include/liwus/math.h ../../include/liwus/stdlib.h
	$(CC) $(CFLAGS) -c $< -o $@

env.o: env.c ../../include/liwus/stdlib.h ../../include/liwus/string.h
	$(CC) $(CFLAGS) -c $< -o $@

qsort.o: qsort.c ../../include/liwus/stdlib.h
	$(CC) $(CFLAGS) -c $< -o $@

signal.o: signal.c ../../include/libc/signal.h ../../include/liwus/stdio.h
	$(CC) $(CFLAGS) -c $< -o $@

webdom_entities.inc: ../../scripts/gen_webdom_entities.py
	python3 $<

webdom.o: webdom.c webdom_entities.inc ../../include/liwus/webdom.h ../../include/liwus/stdlib.h ../../include/liwus/string.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o libliwus.a

.PHONY: all clean
