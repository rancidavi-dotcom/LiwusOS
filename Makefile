all: $(LIBGLOSS_A) zlib libpng libjpeg $(CRT0_OBJ) liwusos.iso
CC = gcc
AR = ar
HOSTCC = gcc
M32 = -m32
M64 = -m64 -mno-red-zone

KERNEL_INCLUDES = -Iinclude -Iinclude/kernel -Iinclude/drivers -Iinclude/fs -Iinclude/gui -Iinclude/uapi \
  -Isrc/kernel -Isrc/kernel/gui -Isrc/kernel/gui/core -Isrc/kernel/gui/scene -Isrc/kernel/gui/render \
  -Isrc/kernel/gui/input -Isrc/kernel/gui/input/tools -Isrc/kernel/gui/widgets -Isrc/kernel/gui/layout \
  -Isrc/kernel/gui/window -Isrc/kernel/gui/assets -Isrc/kernel/gui/math -Isrc/kernel/gui/apps \
  -Isrc/kernel/terminal -Isrc/drivers -Isrc/fs -Isrc/net

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra $(KERNEL_INCLUDES) $(M64) -fno-pie -fno-pic -mcmodel=large -mno-sse -mno-sse2 -mno-mmx
LDFLAGS = -Wl,-no-pie
USER_CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isdk/include -m64 -mno-red-zone -fno-pie -fno-pic
LIBGCC = -lgcc
NEWLIB_DIR = sdk/lib
LIBC_A = $(NEWLIB_DIR)/libc.a
LIBM_A = $(NEWLIB_DIR)/libm.a
LIBGLOSS_A = sdk/lib/libgloss.a
CRT0_OBJ = libgloss/crt0.o

$(CRT0_OBJ): libgloss/crt0.S
	$(CC) -c $< -o $@ $(USER_CFLAGS)

SRC_DIR = src
BOOT_DIR = $(SRC_DIR)/boot
KERNEL_DIR = $(SRC_DIR)/kernel
DRIVERS_DIR = $(SRC_DIR)/drivers
FS_DIR = $(SRC_DIR)/fs
NET_DIR = $(SRC_DIR)/net
APPS_DIR = $(SRC_DIR)/apps
OBJ_DIR = obj

KERNEL_BIN = kernel.bin
ISO_IMAGE = liwusos.iso
CALC_ELF = apps/calc/calc.elf
HELLO_ELF = apps/hello/hello.elf
DOOMPROBE_ELF = apps/doomprobe/doomprobe.elf
LUA_ELF = apps/lua/lua.elf
CRUN_ELF = apps/c4/crun.elf
EDITOR_NANO_ELF = apps/editor_nano/editor_nano.elf
TCC_ELF = apps/tcc/tcc.elf
NANO_ELF = apps/kilo/kilo.elf
DEMO_GUI_ELF = apps/demo_gui/demo_gui.elf
LDE_ELF = lde/src/lde.elf


BOOT_SRCS = $(BOOT_DIR)/boot.s $(BOOT_DIR)/interrupt.s
KERNEL_SRCS = $(wildcard $(KERNEL_DIR)/*.c) $(wildcard $(KERNEL_DIR)/*.s) \
              $(wildcard $(KERNEL_DIR)/arch/x86_64/*.c) $(wildcard $(KERNEL_DIR)/arch/x86_64/*.s) \
              $(wildcard $(KERNEL_DIR)/core/*.c) \
              $(wildcard $(KERNEL_DIR)/lib/*.c) $(wildcard $(KERNEL_DIR)/lib/*.s) \
              $(wildcard $(KERNEL_DIR)/mm/*.c) \
              $(wildcard $(KERNEL_DIR)/sched/*.c) \
              $(wildcard $(KERNEL_DIR)/terminal/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/core/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/scene/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/render/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/input/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/input/tools/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/widgets/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/layout/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/window/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/assets/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/apps/*.c) \
              $(wildcard $(KERNEL_DIR)/gui/math/*.c) $(wildcard $(KERNEL_DIR)/gui/math/*.s) \
              src/kernel/gui/core/app_registry.o
DRIVERS_SRCS = $(wildcard $(DRIVERS_DIR)/*.c)
FS_SRCS = $(wildcard $(FS_DIR)/*.c)
NET_SRCS = $(wildcard $(NET_DIR)/*.c)
APPS_SRCS = $(filter-out $(APPS_DIR)/liw_app.c $(APPS_DIR)/editor.c, $(wildcard $(APPS_DIR)/*.c))

KERNEL_C_SRCS = $(KERNEL_SRCS) $(DRIVERS_SRCS) $(FS_SRCS) $(NET_SRCS) $(APPS_SRCS)
KERNEL_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(filter %.c,$(KERNEL_C_SRCS))) \
              $(patsubst %.s,$(OBJ_DIR)/%.o,$(filter %.s,$(KERNEL_C_SRCS))) \
              $(patsubst %.s,$(OBJ_DIR)/%.o,$(BOOT_SRCS)) \
              $(OBJ_DIR)/$(DRIVERS_DIR)/font.o

LIBGLOSS_SRCS = libgloss/syscalls.c
LIBGLOSS_OBJS = $(OBJ_DIR)/libgloss/syscalls.o

LUA_DIR = third_party/lua/src
LUA_CFLAGS = $(USER_CFLAGS) -I$(LUA_DIR) -DLUA_USE_C89
LUA_ALL_SRCS = $(wildcard $(LUA_DIR)/*.c)
LUA_SRCS = $(filter-out $(LUA_DIR)/lua.c $(LUA_DIR)/luac.c, $(LUA_ALL_SRCS)) apps/lua/lua_main.c

ZLIB_LIB = sdk/lib/libz.a
PNG_LIB = sdk/lib/libpng.a
JPEG_LIB = sdk/lib/libjpeg.a

ZLIB_DIR = third_party/zlib
PNG_DIR = third_party/libpng
JPEG_DIR = third_party/libjpeg

.PHONY: all run run-serial run-log clean zlib libpng libjpeg

# ---- Audio (AC'97 -> host) ----
# O kernel toca o audio pela placa virtual AC'97; para ouvir no host o
# QEMU precisa de um "audio backend":
#   Windows (MSYS2/Git Bash) -> dsound
#   WSL2 com WSLg             -> pa (PulseAudio -> alto-falantes do Windows)
#   outro Linux               -> sdl
# Para forcar:  make run AUDIO_BACKEND=none   (e.g. sem som)
UNAME_S := $(shell uname -s)
AUDIO_BACKEND ?= $(if $(findstring MINGW,$(UNAME_S)),dsound,$(if $(wildcard /mnt/wslg/PulseServer),pa,sdl))
AUDIO_FLAGS = -audiodev $(AUDIO_BACKEND),id=aud0 -device AC97,audiodev=aud0


zlib:
	@if [ ! -f $(ZLIB_LIB) ] && [ -d $(ZLIB_DIR) ]; then \
		cd $(ZLIB_DIR) && CHOST=i686-elf CC=$(CC) CFLAGS="$(USER_CFLAGS) -I$(CURDIR)/sdk/include" ./configure --static --prefix=$(CURDIR)/sdk && $(MAKE) libz.a && \
		cp libz.a ../../$(ZLIB_LIB) && cp *.h ../../sdk/include/; \
	fi

libpng: zlib $(CRT0_OBJ)
	@if [ ! -f $(PNG_LIB) ] && [ -d $(PNG_DIR) ]; then \
		cd $(PNG_DIR) && \
		CPPFLAGS="-I$(CURDIR)/sdk/include" \
		LDFLAGS="-L$(CURDIR)/sdk/lib -nostdlib" \
		LIBS="-lgloss -lc -lm -lgcc" \
		./configure --host=i686-elf --prefix=$(CURDIR)/sdk \
		--enable-static --disable-shared \
		--with-zlib-prefix=$(CURDIR)/sdk \
		ac_cv_func_malloc_0_nonnull=yes \
		ac_cv_func_realloc_0_nonnull=yes \
		ac_cv_lib_z_zlibVersion=yes && \
		$(MAKE) libpng16.la && \
		cp .libs/libpng16.a ../../$(PNG_LIB) && cp *.h ../../sdk/include/; \
	fi

libjpeg: $(CRT0_OBJ)
	@if [ ! -f $(JPEG_LIB) ] && [ -d $(JPEG_DIR) ]; then \
		cd $(JPEG_DIR) && \
		./configure --host=i686-elf --prefix=$(CURDIR)/sdk \
		--enable-static --disable-shared \
		CC=$(CC) CFLAGS="$(USER_CFLAGS) -I$(CURDIR)/sdk/include" \
		LDFLAGS="-L$(CURDIR)/sdk/lib -nostdlib" \
		LIBS="-lgloss -lc -lm -lgcc" \
		ac_cv_func_malloc_0_nonnull=yes \
		ac_cv_func_realloc_0_nonnull=yes && \
		$(MAKE) libjpeg.la && \
		cp .libs/libjpeg.a ../../$(JPEG_LIB) && cp *.h ../../sdk/include/; \
	fi

$(KERNEL_BIN): $(KERNEL_OBJS) $(BOOT_DIR)/linker.ld
	$(CC) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) $(LDFLAGS) -nostdlib -static -Wl,--build-id=none $(KERNEL_OBJS) $(LIBGCC)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

# mp3.c uses float (minimp3). GCC 15 refuses float returns under -mno-sse,
# so this translation unit gets SSE enabled (still no SIMD intrinsics via
# MINIMP3_NO_SIMD). -mstackrealign guarantees 16-byte stack alignment in
# every function, so its movaps stores never fault (#GP) on the misaligned
# task stacks some allocations produce.
$(OBJ_DIR)/$(DRIVERS_DIR)/mp3.o: $(DRIVERS_DIR)/mp3.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS) -msse -mstackrealign

$(OBJ_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(M64)

$(OBJ_DIR)/$(DRIVERS_DIR)/font.o: $(DRIVERS_DIR)/font.psf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(LIBGLOSS_A): $(LIBGLOSS_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(LIBGLOSS_OBJS)

$(LIBGLOSS_OBJS): libgloss/syscalls.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_CFLAGS)

LIBS = $(LIBC_A) $(LIBM_A) $(LIBGLOSS_A) $(LIBGCC)

$(HELLO_ELF): apps/hello/hello.c $(CRT0_OBJ) $(LIBGLOSS_A) $(LIBC_A) $(LIBM_A)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/hello/hello.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(DOOMPROBE_ELF): apps/doomprobe/doomprobe.c $(CRT0_OBJ) $(LIBGLOSS_A)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/doomprobe/doomprobe.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(LUA_ELF): $(LUA_SRCS) $(CRT0_OBJ) $(LIBGLOSS_A)
	$(CC) $(LUA_CFLAGS) -nostdlib -static $(CRT0_OBJ) $(LUA_SRCS) -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(EDITOR_NANO_ELF): apps/editor_nano/editor_nano.c apps/editor_nano/font.h $(CRT0_OBJ) $(LIBGLOSS_A)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/editor_nano/editor_nano.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(NANO_ELF): apps/kilo/kilo.c $(CRT0_OBJ) $(LIBGLOSS_A) $(LIBC_A) $(LIBM_A)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/kilo/kilo.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(CRUN_ELF): apps/c4/c4.c $(CRT0_OBJ) $(LIBGLOSS_A)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/c4/c4.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(CALC_ELF): apps/calc/calc.c $(CRT0_OBJ) $(LIBGLOSS_A) sdk/lib/libliwus_gui.a
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/calc/calc.c -L$(NEWLIB_DIR) -Lsdk/lib -lliwus_gui -lgloss -lc -lm -o $@ $(LIBGCC)

$(TCC_ELF): apps/tcc/tcc.c $(CRT0_OBJ) $(LIBGLOSS_A) $(LIBC_A) $(LIBM_A)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -DONE_SOURCE=1 -DTCC_TARGET_X86_64 -DCONFIG_TCCDIR=\"/usr/lib/tcc\" -DCONFIG_TCC_SEMLOCK=0 -DCONFIG_TCC_BACKTRACE=0 -DCONFIG_TCC_BCHECK=0 -nostdlib -static $(CRT0_OBJ) apps/tcc/tcc.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

sdk/lib/libliwus_gui.a: sdk/lib/liwus_gui.c
	$(CC) -c $< -o sdk/lib/liwus_gui.o $(USER_CFLAGS) -I$(CURDIR)/sdk/include
	$(AR) rcs $@ sdk/lib/liwus_gui.o

$(DEMO_GUI_ELF): apps/demo_gui/demo_gui.c $(CRT0_OBJ) $(LIBGLOSS_A) sdk/lib/libliwus_gui.a
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) apps/demo_gui/demo_gui.c -L$(NEWLIB_DIR) -Lsdk/lib -lliwus_gui -lgloss -lc -lm -o $@ $(LIBGCC)

$(LDE_ELF): lde/src/main.c lde/src/system_bridge.c $(CRT0_OBJ) $(LIBGLOSS_A) sdk/lib/libliwus_gui.a
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -nostdlib -static $(CRT0_OBJ) lde/src/main.c lde/src/system_bridge.c -L$(NEWLIB_DIR) -Lsdk/lib -lliwus_gui -lgloss -lc -lm -o $@ $(LIBGCC)

$(ISO_IMAGE): $(KERNEL_BIN) $(BOOT_DIR)/test.elf $(DEMO_GUI_ELF) $(LDE_ELF)
	$(HOSTCC) -Iinclude -Iinclude/uapi sdk/tools/liw-builder.c -o sdk/tools/liw-builder
	$(HOSTCC) sdk/tools/img-gen.c -o sdk/tools/img-gen
	./sdk/tools/liw-builder src/boot/test.liw src/boot/test.elf src/boot/test_manifest.json
	grub-file --is-x86-multiboot2 $(KERNEL_BIN)
	mkdir -p repo
	./sdk/tools/img-gen
	if [ -f $(DEMO_GUI_ELF) ]; then cp $(DEMO_GUI_ELF) repo/demo_gui; fi
	if [ -f $(LDE_ELF) ]; then cp $(LDE_ELF) repo/lde; fi

	tar -cvf initrd.tar -C repo . --format=ustar
	mkdir -p isodir/boot/grub
	cp $(KERNEL_BIN) isodir/boot/kernel.bin
	cp $(BOOT_DIR)/grub.cfg isodir/boot/grub/grub.cfg
	cp initrd.tar isodir/boot/initrd.tar
	# Create SDFS disk image for persistent storage (only if not exists)
	if [ ! -f liwus_disk.img ]; then dd if=/dev/zero of=liwus_disk.img bs=1M count=64 2>/dev/null; fi
	grub-mkrescue -o $(ISO_IMAGE) isodir



# ============================================================
# Test ELF
# ============================================================

$(BOOT_DIR)/test.elf: $(BOOT_DIR)/test.s
	$(CC) -nostdlib -static $< -o $@ -m32 -Ttext 0x100000

run: $(ISO_IMAGE)
	if [ ! -f liwus_disk.img ]; then dd if=/dev/zero of=liwus_disk.img bs=1M count=64 2>/dev/null; fi
	PULSE_SERVER=$(if $(filter pa,$(AUDIO_BACKEND)),/mnt/wslg/PulseServer,) \
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -drive id=disk,file=liwus_disk.img,if=none,format=raw -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 512 $(AUDIO_FLAGS)

run-serial: $(ISO_IMAGE)
	if [ ! -f liwus_disk.img ]; then dd if=/dev/zero of=liwus_disk.img bs=1M count=64 2>/dev/null; fi
	PULSE_SERVER=$(if $(filter pa,$(AUDIO_BACKEND)),/mnt/wslg/PulseServer,) \
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -drive id=disk,file=liwus_disk.img,if=none,format=raw -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 512 $(AUDIO_FLAGS) -serial stdio -d guest_errors -no-reboot

run-log: $(ISO_IMAGE)
	if [ ! -f liwus_disk.img ]; then dd if=/dev/zero of=liwus_disk.img bs=1M count=64 2>/dev/null; fi
	PULSE_SERVER=$(if $(filter pa,$(AUDIO_BACKEND)),/mnt/wslg/PulseServer,) \
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -drive id=disk,file=liwus_disk.img,if=none,format=raw -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 512 $(AUDIO_FLAGS) -serial file:qemu_serial.log -D qemu_debug.log -d int,cpu_reset

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(KERNEL_BIN) $(ISO_IMAGE) initrd.tar src/boot/test.elf src/boot/test.liw $(CALC_ELF) $(HELLO_ELF) $(DOOMPROBE_ELF) $(LUA_ELF) $(CRUN_ELF) $(EDITOR_NANO_ELF) $(DEMO_GUI_ELF) $(LDE_ELF) $(LIBGLOSS_A) $(LIBGLOSS_OBJS) $(CRT0_OBJ)
	@echo "  NOTA: liwus_disk.img preservado (remova manualmente se quiser disco limpo)"
	rm -f sdk/tools/liw-builder sdk/tools/img-gen
	rm -rf isodir repo
