CC ?= gcc
LD ?= ld
SDK_DIR ?= ../../build/liwus-sdk
UPSTREAM ?= ../../third_party/craft
OBJ_DIR ?= $(SDK_DIR)/craft-obj
LXE = $(SDK_DIR)/bin/craft.lxe

CFLAGS = -std=gnu99 -O3 -ffast-math -ffreestanding -fno-builtin -Wall -Wextra \
	-Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
	-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
	-DLIWUSOS_SYNC_CHUNK_WORKERS -include config.h \
	-I. -Icompat -I$(UPSTREAM)/src -I$(UPSTREAM)/deps/noise \
	-I$(UPSTREAM)/deps/lodepng -I$(SDK_DIR)/include

SRCS = $(UPSTREAM)/src/main.c $(UPSTREAM)/src/cube.c \
	$(UPSTREAM)/src/item.c $(UPSTREAM)/src/map.c \
	$(UPSTREAM)/src/matrix.c $(UPSTREAM)/src/ring.c \
	$(UPSTREAM)/src/sign.c $(UPSTREAM)/src/util.c \
	$(UPSTREAM)/src/world.c $(UPSTREAM)/deps/noise/noise.c \
	$(UPSTREAM)/deps/lodepng/lodepng.c platform.c softgl.c \
	offline_services.c compat/string_extra.c
OBJS = $(addprefix $(OBJ_DIR)/,$(notdir $(SRCS:.c=.o)))
CRAFT_HEADERS = $(wildcard *.h compat/*.h compat/GL/*.h \
	compat/GLFW/*.h compat/curl/*.h) $(UPSTREAM)/src/config.h

.PHONY: all clean
all: $(LXE)

$(OBJ_DIR)/%.o: $(UPSTREAM)/src/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/noise.o: $(UPSTREAM)/deps/noise/noise.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/lodepng.o: $(UPSTREAM)/deps/lodepng/lodepng.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(OBJ_DIR)/platform.o: platform.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/softgl.o: softgl.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(OBJ_DIR)/offline_services.o: offline_services.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/string_extra.o: compat/string_extra.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LXE): $(OBJS) $(SDK_DIR)/lib/libliwus.a $(SDK_DIR)/lib/libopenlibm.a \
		$(SDK_DIR)/lib/crt0.o $(SDK_DIR)/share/liwus/elf64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -nostdlib -static --build-id=none \
		-T $(SDK_DIR)/share/liwus/elf64.ld -o $@ \
		$(SDK_DIR)/lib/crt0.o $(OBJS) $(SDK_DIR)/lib/libliwus.a \
		$(SDK_DIR)/lib/libopenlibm.a

clean:
	rm -rf $(OBJ_DIR) $(LXE)

$(OBJS): $(CRAFT_HEADERS) Makefile
