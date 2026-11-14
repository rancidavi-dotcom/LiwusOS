CC ?= gcc
AR ?= ar
M64 ?= -m64 -mno-red-zone

ROOT_DIR ?= $(abspath ../../../..)

INCLUDES = -I. -I$(ROOT_DIR)/include -I$(ROOT_DIR)/include/libc -I$(ROOT_DIR)/include/kernel -I$(ROOT_DIR)/include/drivers -I$(ROOT_DIR)/include/gui

CFLAGS ?= -std=gnu99 -ffreestanding -O2 $(M64) -fno-pie -fno-pic -mcmodel=large \
          -mno-sse -mno-sse2 -mno-mmx -fno-stack-protector -fno-asynchronous-unwind-tables \
          -DLV_CONF_INCLUDE_SIMPLE -DLODEPNG_NO_COMPILE_DISK -Wno-unused-parameter -Wno-unused-function \
          -Wno-unused-but-set-variable -Wno-parentheses -Wno-sign-compare

SRCS = $(shell find src -name "*.c" \
         ! -path "*/arm2d/*" \
         ! -path "*/nxp/*" \
         ! -path "*/renesas/*" \
         ! -path "*/sdl/*" \
         ! -path "*/stm32_dma2d/*" \
         ! -path "*/swm341_dma2d/*" \
         ! -path "*/extra/libs/*" \
         ! -path "*/extra/others/fragment/*" \
         ! -path "*/extra/others/monkey/*" \
         ! -path "*/extra/others/ime/*" \
         ! -path "*/extra/others/imgfont/*") \
       $(wildcard src/extra/libs/png/*.c) \
       $(wildcard src/extra/libs/sjpg/*.c)

OBJS = $(patsubst %.c,build/%.o,$(SRCS))
TARGET = liblvgl.a

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJS)

build/%.o: %.c lv_conf.h lvgl.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf build $(TARGET)

.PHONY: all clean
