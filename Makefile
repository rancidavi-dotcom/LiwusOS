CC = i686-elf-gcc
AR = i686-elf-ar
HOSTCC = gcc

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude -Isdk/libc/include -Ithird_party/lvgl -Ithird_party/lvgl/src -DLV_CONF_INCLUDE_SIMPLE
USER_CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isdk/include -Isdk/libc/include
LIBGCC = -lgcc
CRT0_OBJ = sdk/lib/crt0.o

$(CRT0_OBJ): sdk/lib/crt0.s
	$(CC) -c $< -o $@

SRC_DIR = src
BOOT_DIR = $(SRC_DIR)/boot
KERNEL_DIR = $(SRC_DIR)/kernel
DRIVERS_DIR = $(SRC_DIR)/drivers
FS_DIR = $(SRC_DIR)/fs
NET_DIR = $(SRC_DIR)/net
GUI_DIR = $(SRC_DIR)/gui
APPS_DIR = $(SRC_DIR)/apps
OBJ_DIR = obj

KERNEL_BIN = kernel.bin
ISO_IMAGE = liwusos.iso
LIW_ELF = liw.elf
HELLO_ELF = apps/hello/hello.elf
DOOMPROBE_ELF = apps/doomprobe/doomprobe.elf
LUA_ELF = apps/lua/lua.elf
DOOMGENERIC_ELF = apps/doomgeneric/doomgeneric.elf
CRUN_ELF = apps/c4/crun.elf
VIEW_ELF = apps/view/view.elf
EDITOR_NANO_ELF = apps/editor_nano/editor_nano.elf

TCC_DIR = third_party/tcc
TCC_BIN = $(TCC_DIR)/tcc

BOOT_SRCS = $(BOOT_DIR)/boot.s $(BOOT_DIR)/interrupt.s
KERNEL_SRCS = $(wildcard $(KERNEL_DIR)/*.c) $(wildcard $(KERNEL_DIR)/*.s)
DRIVERS_SRCS = $(wildcard $(DRIVERS_DIR)/*.c)
FS_SRCS = $(wildcard $(FS_DIR)/*.c)
NET_SRCS = $(wildcard $(NET_DIR)/*.c)
GUI_SRCS = $(wildcard $(GUI_DIR)/*.c)
APPS_SRCS = $(filter-out $(APPS_DIR)/liw_app.c, $(wildcard $(APPS_DIR)/*.c))

LVGL_DIR = third_party/lvgl
LVGL_CORE_SRCS = $(shell find $(LVGL_DIR)/src/core -name '*.c')
LVGL_DISPLAY_SRCS = $(shell find $(LVGL_DIR)/src/display -name '*.c')
LVGL_DRAW_SRCS = $(shell find $(LVGL_DIR)/src/draw -maxdepth 1 -name '*.c') \
		 $(shell find $(LVGL_DIR)/src/draw/sw -maxdepth 1 -name '*.c') \
		 $(shell find $(LVGL_DIR)/src/draw/sw/blend -name '*.c') \
		 $(shell find $(LVGL_DIR)/src/draw/convert -maxdepth 1 -name '*.c')
LVGL_FONT_SRCS = $(shell find $(LVGL_DIR)/src/font -name '*.c')
LVGL_INDEV_SRCS = $(shell find $(LVGL_DIR)/src/indev -name '*.c')
LVGL_LAYOUT_SRCS = $(shell find $(LVGL_DIR)/src/layouts -name '*.c')
LVGL_MISC_SRCS = $(shell find $(LVGL_DIR)/src/misc -name '*.c')
LVGL_STDLIB_SRCS = $(LVGL_DIR)/src/stdlib/lv_mem.c \
		   $(shell find $(LVGL_DIR)/src/stdlib/builtin -name '*.c')
LVGL_WIDGET_SRCS = $(LVGL_DIR)/src/widgets/button/lv_button.c \
		   $(LVGL_DIR)/src/widgets/label/lv_label.c \
		   $(LVGL_DIR)/src/widgets/textarea/lv_textarea.c
LVGL_OSAL_SRCS = $(LVGL_DIR)/src/osal/lv_os.c \
		 $(LVGL_DIR)/src/osal/lv_os_none.c
LVGL_TICK_SRCS = $(LVGL_DIR)/src/tick/lv_tick.c
LVGL_INIT_SRCS = $(LVGL_DIR)/src/lv_init.c
LVGL_SRCS = $(LVGL_INIT_SRCS) $(LVGL_CORE_SRCS) $(LVGL_DISPLAY_SRCS) \
	    $(LVGL_DRAW_SRCS) $(LVGL_FONT_SRCS) $(LVGL_INDEV_SRCS) \
	    $(LVGL_LAYOUT_SRCS) $(LVGL_MISC_SRCS) $(LVGL_STDLIB_SRCS) \
	    $(LVGL_WIDGET_SRCS) $(LVGL_OSAL_SRCS) $(LVGL_TICK_SRCS)

KERNEL_C_SRCS = $(KERNEL_SRCS) $(DRIVERS_SRCS) $(FS_SRCS) $(NET_SRCS) $(GUI_SRCS) $(APPS_SRCS) $(LVGL_SRCS)
KERNEL_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(filter %.c,$(KERNEL_C_SRCS))) \
              $(patsubst %.s,$(OBJ_DIR)/%.o,$(filter %.s,$(KERNEL_C_SRCS))) \
              $(patsubst %.s,$(OBJ_DIR)/%.o,$(BOOT_SRCS)) \
              $(OBJ_DIR)/$(DRIVERS_DIR)/font.o

SDK_LIBC_SRCS = $(wildcard sdk/libc/*.c)
SDK_LIBC_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SDK_LIBC_SRCS))
SDK_LIB = sdk/lib/libliwc.a

DOOM_DIR = third_party/doomgeneric/doomgeneric
DOOMGENERIC_CFLAGS = $(USER_CFLAGS) -I$(DOOM_DIR) -Iapps/doomgeneric -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
DOOM_ALL_SRCS = $(wildcard $(DOOM_DIR)/*.c)
DOOM_EXCLUDE = $(DOOM_DIR)/doomgeneric_allegro.c $(DOOM_DIR)/doomgeneric_emscripten.c \
               $(DOOM_DIR)/doomgeneric_linuxvt.c $(DOOM_DIR)/doomgeneric_sdl.c \
               $(DOOM_DIR)/doomgeneric_soso.c $(DOOM_DIR)/doomgeneric_sosox.c \
               $(DOOM_DIR)/doomgeneric_win.c $(DOOM_DIR)/doomgeneric_xlib.c \
               $(DOOM_DIR)/i_allegromusic.c $(DOOM_DIR)/i_allegrosound.c \
               $(DOOM_DIR)/i_sdlmusic.c $(DOOM_DIR)/i_sdlsound.c
DOOM_SRCS = $(filter-out $(DOOM_EXCLUDE), $(DOOM_ALL_SRCS)) \
	    apps/doomgeneric/doomgeneric_liwus.c

LUA_DIR = third_party/lua/src
LUA_CFLAGS = $(USER_CFLAGS) -I$(LUA_DIR) -DLUA_USE_C89
LUA_ALL_SRCS = $(wildcard $(LUA_DIR)/*.c)
LUA_SRCS = $(filter-out $(LUA_DIR)/lua.c $(LUA_DIR)/luac.c, $(LUA_ALL_SRCS)) apps/lua/lua_main.c

SDK_LIB = sdk/lib/libliwc.a
ZLIB_LIB = sdk/lib/libz.a
PNG_LIB = sdk/lib/libpng.a
JPEG_LIB = sdk/lib/libjpeg.a

ZLIB_DIR = third_party/zlib
PNG_DIR = third_party/libpng
JPEG_DIR = third_party/libjpeg

.PHONY: all run clean tcc zlib libpng libjpeg

all: $(SDK_LIB) zlib libpng libjpeg $(ISO_IMAGE)

zlib:
	@if [ -d $(ZLIB_DIR) ]; then \
		cd $(ZLIB_DIR) && CHOST=i686-elf CC=$(CC) CFLAGS="$(USER_CFLAGS) -I$(CURDIR)/sdk/libc/include" ./configure --static --prefix=$(CURDIR)/sdk && $(MAKE) libz.a && \
		cp libz.a ../../$(ZLIB_LIB) && cp *.h ../../sdk/libc/include/; \
	fi

libpng: zlib $(SDK_LIB) $(CRT0_OBJ)
	@if [ -d $(PNG_DIR) ]; then \
		cd $(PNG_DIR) && \
		CPPFLAGS="-I$(CURDIR)/sdk/libc/include" \
		LDFLAGS="-L$(CURDIR)/sdk/lib -nostdlib" \
		LIBS="-l:libliwc.a -lgcc" \
		./configure --host=i686-elf --prefix=$(CURDIR)/sdk \
		--enable-static --disable-shared \
		--with-zlib-prefix=$(CURDIR)/sdk \
		ac_cv_func_malloc_0_nonnull=yes \
		ac_cv_func_realloc_0_nonnull=yes \
		ac_cv_lib_z_zlibVersion=yes && \
		$(MAKE) libpng16.la && \
		cp .libs/libpng16.a ../../$(PNG_LIB) && cp *.h ../../sdk/libc/include/; \
	fi

libjpeg: $(SDK_LIB) $(CRT0_OBJ)
	@if [ -d $(JPEG_DIR) ]; then \
		cd $(JPEG_DIR) && \
		./configure --host=i686-elf --prefix=$(CURDIR)/sdk \
		--enable-static --disable-shared \
		CC=$(CC) CFLAGS="$(USER_CFLAGS) -I$(CURDIR)/sdk/libc/include" \
		LDFLAGS="-L$(CURDIR)/sdk/lib -nostdlib" \
		LIBS="-l:libliwc.a -lgcc" \
		ac_cv_func_malloc_0_nonnull=yes \
		ac_cv_func_realloc_0_nonnull=yes && \
		$(MAKE) libjpeg.la && \
		cp .libs/libjpeg.a ../../$(JPEG_LIB) && cp *.h ../../sdk/libc/include/; \
	fi

tcc:
	cd $(TCC_DIR) && ./configure --cc=$(CC) --cpu=i386 --triplet=i686-elf --prefix=/usr/local --extra-cflags="-ffreestanding -I$(CURDIR)/sdk/libc/include" && $(MAKE) tcc

$(KERNEL_BIN): $(KERNEL_OBJS) $(BOOT_DIR)/linker.ld
	$(CC) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) -nostdlib $(KERNEL_OBJS) $(LIBGCC)

$(OBJ_DIR)/sdk/libc/%.o: sdk/libc/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_CFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

$(OBJ_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

$(OBJ_DIR)/$(DRIVERS_DIR)/font.o: $(DRIVERS_DIR)/font.psf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf32-i386 -B i386 $< $@

$(SDK_LIB): $(SDK_LIBC_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(SDK_LIBC_OBJS)
	cp $@ sdk/lib/libm.a
	cp $@ sdk/lib/libc.a

$(LIW_ELF): src/apps/liw_app.c
	$(CC) $(USER_CFLAGS) -nostdlib -static $< -o $@ $(LIBGCC)

$(HELLO_ELF): apps/hello/hello.c $(SDK_LIB) $(CRT0_OBJ)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/hello/hello.c $(SDK_LIB) -o $@ $(LIBGCC)

$(DOOMPROBE_ELF): apps/doomprobe/doomprobe.c $(SDK_LIB) $(CRT0_OBJ)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/doomprobe/doomprobe.c $(SDK_LIB) -o $@ $(LIBGCC)

$(LUA_ELF): $(LUA_SRCS) $(SDK_LIB) $(CRT0_OBJ)
	$(CC) $(LUA_CFLAGS) -nostdlib -static $(CRT0_OBJ) $(LUA_SRCS) $(SDK_LIB) -o $@ $(LIBGCC)

$(DOOMGENERIC_ELF): $(DOOM_SRCS) $(SDK_LIB) $(CRT0_OBJ)
	$(CC) $(DOOMGENERIC_CFLAGS) -nostdlib -static $(CRT0_OBJ) $(DOOM_SRCS) $(SDK_LIB) -o $@ $(LIBGCC)

$(EDITOR_NANO_ELF): apps/editor_nano/editor_nano.c apps/editor_nano/font.h $(SDK_LIB) $(CRT0_OBJ)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/editor_nano/editor_nano.c $(SDK_LIB) -o $@ $(LIBGCC)

$(CRUN_ELF): apps/c4/c4.c $(SDK_LIB) $(CRT0_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/c4/c4.c $(SDK_LIB) -o $@ $(LIBGCC)

$(VIEW_ELF): apps/view/view.c $(SDK_LIB) $(CRT0_OBJ) zlib libpng libjpeg
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -I$(CURDIR)/sdk/libc/include -nostdlib -static $(CRT0_OBJ) apps/view/view.c $(PNG_LIB) $(JPEG_LIB) $(ZLIB_LIB) $(SDK_LIB) -o $@ $(LIBGCC)

$(ISO_IMAGE): $(KERNEL_BIN) $(BOOT_DIR)/test.elf $(SDK_LIB) $(LIW_ELF) $(HELLO_ELF) $(DOOMPROBE_ELF) $(LUA_ELF) $(DOOMGENERIC_ELF) $(CRUN_ELF) $(VIEW_ELF) $(EDITOR_NANO_ELF)
	$(HOSTCC) -Iinclude sdk/tools/liw-builder.c -o sdk/tools/liw-builder
	$(HOSTCC) sdk/tools/img-gen.c -o sdk/tools/img-gen
	./sdk/tools/liw-builder src/boot/test.liw src/boot/test.elf src/boot/test_manifest.json
	grub-file --is-x86-multiboot $(KERNEL_BIN)
	mkdir -p repo
	./sdk/tools/img-gen
	if [ -f assets/LiwusOSlogo.png ]; then cp assets/LiwusOSlogo.png repo/logo.png; fi
	cp $(LIW_ELF) repo/liw
	cp $(HELLO_ELF) repo/hello.liwpkg
	cp $(DOOMPROBE_ELF) repo/doomprobe.liwpkg
	cp $(LUA_ELF) repo/lua
	cp $(CRUN_ELF) repo/crun
	cp $(VIEW_ELF) repo/view.liwpkg
	cp apps/lua/hello.lua repo/hello.lua
	cp $(DOOMGENERIC_ELF) repo/doomgeneric
	cp $(EDITOR_NANO_ELF) repo/editor
	if [ -f freedoom1.wad ]; then cp freedoom1.wad repo/freedoom1.wad; fi
	tar -cvf initrd.tar -C repo . --format=ustar
	mkdir -p isodir/boot/grub
	cp $(KERNEL_BIN) isodir/boot/kernel.bin
	cp $(BOOT_DIR)/grub.cfg isodir/boot/grub/grub.cfg
	cp initrd.tar isodir/boot/initrd.tar
	grub-mkrescue -o $(ISO_IMAGE) isodir

$(BOOT_DIR)/test.elf: $(BOOT_DIR)/test.s
	$(CC) -nostdlib -static $< -o $@ $(LIBGCC)

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(KERNEL_BIN) $(ISO_IMAGE) initrd.tar liwus_disk.img src/boot/test.elf src/boot/test.liw $(LIW_ELF) $(HELLO_ELF) $(DOOMPROBE_ELF) $(LUA_ELF) $(DOOMGENERIC_ELF) $(CRUN_ELF) $(EDITOR_NANO_ELF)
	rm -f sdk/tools/liw-builder sdk/tools/img-gen
	rm -rf isodir repo
