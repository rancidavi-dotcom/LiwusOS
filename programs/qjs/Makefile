APP_NAME = qjs
REPO_ROOT := $(abspath ../..)
SDK_DIR ?= $(REPO_ROOT)/build/liwus-sdk
QJS_DIR := $(REPO_ROOT)/third_party/quickjs
OLM_DIR := $(REPO_ROOT)/third_party/openlibm
OUT := $(SDK_DIR)/bin/$(APP_NAME).lxe
OBJ_DIR := $(REPO_ROOT)/build/qjs
QJS_OBJECTS := $(OBJ_DIR)/quickjs.o $(OBJ_DIR)/libregexp.o \
               $(OBJ_DIR)/libunicode.o $(OBJ_DIR)/dtoa.o $(OBJ_DIR)/cutils.o \
               $(OBJ_DIR)/module_url.o
CFLAGS := -std=gnu99 -O2 -Wall -Wextra -ffreestanding -fno-builtin \
          -fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
          -DCONFIG_LIWUS -DCONFIG_VERSION='"2026-06-04-liwus"' \
          -Dalloca=__builtin_alloca \
          -I$(OLM_DIR)/port/include \
          -I$(SDK_DIR)/include -I$(OLM_DIR)/include \
          -I$(OLM_DIR)/src -I$(QJS_DIR) \
          -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough \
          -Wno-misleading-indentation -Wno-return-type

.PHONY: all clean
all: $(OUT)

$(OUT): main.c module_url.h $(QJS_OBJECTS) $(SDK_DIR)/lib/libliwus.a \
       $(SDK_DIR)/lib/libopenlibm.a $(SDK_DIR)/lib/crt0.o \
       $(SDK_DIR)/share/liwus/elf64.ld
	@mkdir -p $(dir $@)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
		-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
		-I$(SDK_DIR)/include -isystem $(QJS_DIR) \
		-c main.c -o $(OBJ_DIR)/main.o
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,$(SDK_DIR)/share/liwus/elf64.ld -o $@ \
		$(SDK_DIR)/lib/crt0.o $(OBJ_DIR)/main.o $(QJS_OBJECTS) \
		$(SDK_DIR)/lib/libopenlibm.a $(SDK_DIR)/lib/libliwus.a -lgcc

$(OBJ_DIR)/module_url.o: module_url.c module_url.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/quickjs.o: $(QJS_DIR)/quickjs.c $(QJS_DIR)/quickjs.h Makefile
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(QJS_DIR)/%.c Makefile
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@test ! -f $(OUT) || rm $(OUT)
	@test ! -f $(OBJ_DIR)/main.o || rm $(OBJ_DIR)/main.o
	@test ! -d $(OBJ_DIR) || find $(OBJ_DIR) -type f -name '*.o' -delete
