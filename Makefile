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
  -Isrc/kernel/terminal -Isrc/drivers -Isrc/fs -Isrc/net -Itests

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra $(KERNEL_INCLUDES) $(M64) -fno-pie -fno-pic -mcmodel=large -mno-sse -mno-sse2 -mno-mmx
LDFLAGS = -Wl,-no-pie
USER_CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isdk/include -m64 -mno-red-zone -fno-pie -fno-pic
USER_LDFLAGS = -T sdk/liwus.ld
LIBGCC = -lgcc
NEWLIB_DIR = sdk/lib
LIBC_A = $(NEWLIB_DIR)/libc.a
LIBM_A = $(NEWLIB_DIR)/libm.a
LIBGLOSS_A = sdk/lib/libgloss.a
CRT0_OBJ = libgloss/crt0.o

$(CRT0_OBJ): libgloss/crt0.S
	$(CC) -c $< -o $@ $(USER_CFLAGS)

CRTI_OBJ = libgloss/crti.o
CRTN_OBJ = libgloss/crtn.o

$(CRTI_OBJ): libgloss/crti.S
	$(CC) -c $< -o $@ $(USER_CFLAGS)

$(CRTN_OBJ): libgloss/crtn.S
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
BEARSSL_LIB = sdk/lib/libbearssl.a

ZLIB_DIR = third_party/zlib
PNG_DIR = third_party/libpng
JPEG_DIR = third_party/libjpeg
BEARSSL_DIR = third_party/bearssl

# ============================================================
# Test infrastructure
# ============================================================
TEST_DIR = tests
TEST_FW = $(TEST_DIR)/framework.h

KERNEL_TEST_CFLAGS = $(CFLAGS) -DKERNEL_TEST -I$(TEST_DIR)

KERNEL_TEST_SRCS = $(TEST_DIR)/test_runner_kernel.c \
                   $(TEST_DIR)/test_sdfs_create.c \
                   $(TEST_DIR)/test_sdfs_rw.c \
                   $(TEST_DIR)/test_sdfs_dir.c \
                   $(TEST_DIR)/test_sdfs_rename.c \
                   $(TEST_DIR)/test_sdfs_delete.c \
                   $(TEST_DIR)/test_sdfs_persist.c \
                   $(TEST_DIR)/test_sdfs_diskfull.c \
                   $(TEST_DIR)/test_sdfs_crc32.c \
                   $(TEST_DIR)/test_sdfs_journal.c \
                   $(TEST_DIR)/test_sdfs_perms.c \
                   $(TEST_DIR)/test_sdfs_v2.c \
                   $(TEST_DIR)/test_net_http.c \
                   $(TEST_DIR)/test_sched.c

KERNEL_TEST_OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(KERNEL_TEST_SRCS))

USER_TEST_SRCS = $(TEST_DIR)/test_runner_user.c \
                 $(TEST_DIR)/test_user_open.c \
                 $(TEST_DIR)/test_user_fork.c \
                 $(TEST_DIR)/test_user_pipe.c \
                 $(TEST_DIR)/test_user_misc.c \
                 $(TEST_DIR)/test_user_process.c

TEST_USER_ELF = $(TEST_DIR)/test_runner.elf

.PHONY: all run run-serial run-log run-bios run-lan hw-prep qa-boot-persistence clean zlib libpng libjpeg test test-sdfs test-user test-clean uefi-iso uefi-check run-j1800 run-j1800-bios run-j1800-log

# ---- Audio (Intel HDA -> host) ----
# O hardware universal (PC J1800) usa Intel HDA + codec Realtek ALC269VC,
# por isso o padrao aqui tambem e HDA. Para ouvir no host o QEMU precisa
# de um "audio backend":
#   Windows (MSYS2/Git Bash) -> dsound
#   WSL2 com WSLg             -> pa (PulseAudio -> alto-falantes do Windows)
#   outro Linux               -> sdl
# Para forcar:  make run AUDIO_BACKEND=none   (e.g. sem som)
UNAME_S := $(shell uname -s)
AUDIO_BACKEND ?= $(if $(findstring MINGW,$(UNAME_S)),dsound,$(if $(wildcard /mnt/wslg/PulseServer),pa,sdl))
# Placa de som virtual:
#   hda (padrao)  -> Intel High Definition Audio (controlador + codec duplex)
#   ac97          -> controlador legado Intel 82801AA (teste do driver AC'97)
# Para usar AC'97:  make run AUDIO_DEV=ac97
AUDIO_DEV ?= hda
ifeq ($(AUDIO_DEV),hda)
AUDIO_FLAGS = -audiodev $(AUDIO_BACKEND),id=aud0 -device intel-hda -device hda-duplex,audiodev=aud0
else
AUDIO_FLAGS = -audiodev $(AUDIO_BACKEND),id=aud0 -device AC97,audiodev=aud0
endif

# Armazenamento virtual:
#   ahci (padrao) -> disco SATA no controlador AHCI
#   nvme          -> SSD NVMe (controlador NVMe + namespace)
# Para testar o driver NVMe:  make run DISK_DEV=nvme
DISK_DEV ?= ahci
ifeq ($(DISK_DEV),nvme)
DISK_FLAGS = -drive id=disk,file=liwus_disk.img,if=none,format=raw -device nvme,serial=liwus,drive=disk
else
DISK_FLAGS = -drive id=disk,file=liwus_disk.img,if=none,format=raw -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0
endif

# Rede.
#   NET_MODE=user (padrao) -> user-mode SLIRP: internet simulada (10.0.2.x) +
#                             port-forwards para o host (acessivel via IP do
#                             Windows na rede local).
#   NET_MODE=tap  -> placa TAP: coloca a NIC do OS NA LAN de verdade (L2).
#                    Exige bridge/tap configurado (ver 'make run-lan').
# RTL8139 e o modelo de NIC classico que o driver de rede do kernel suporta.
ifeq ($(NET_MODE),tap)
NET_FLAGS = -netdev tap,id=net0,script=no,downscript=no -device rtl8139,netdev=net0
else
NET_FLAGS = -netdev user,id=net0,restrict=off,hostfwd=tcp::2222-:2222,hostfwd=tcp::8080-:80 -device rtl8139,netdev=net0
endif



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

bearssl: $(CRT0_OBJ)
	@if [ ! -f $(BEARSSL_LIB) ] && [ -d $(BEARSSL_DIR) ]; then \
		cd $(BEARSSL_DIR) && \
		$(MAKE) CC=$(CC) CFLAGS="$(USER_CFLAGS) -U_FORTIFY_SOURCE -fno-stack-protector -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 -I$(CURDIR)/sdk/include" lib && \
		cp build/libbearssl.a ../../$(BEARSSL_LIB) && \
		cp -r inc/* ../../sdk/include/; \
	fi

$(KERNEL_BIN): bearssl $(KERNEL_OBJS) $(KERNEL_TEST_OBJS) $(BOOT_DIR)/linker.ld
	$(CC) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) $(LDFLAGS) -nostdlib -static -Wl,--build-id=none $(KERNEL_OBJS) $(KERNEL_TEST_OBJS) $(BEARSSL_LIB) $(LIBGCC)

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


$(HELLO_ELF): apps/hello/hello.c $(CRT0_OBJ) $(LIBGLOSS_A) $(LIBC_A) $(LIBM_A)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/hello/hello.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(DOOMPROBE_ELF): apps/doomprobe/doomprobe.c $(CRT0_OBJ) $(LIBGLOSS_A)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/doomprobe/doomprobe.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(LUA_ELF): $(LUA_SRCS) $(CRT0_OBJ) $(LIBGLOSS_A)
	$(CC) $(LUA_CFLAGS) -nostdlib -static $(CRT0_OBJ) $(LUA_SRCS) -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(EDITOR_NANO_ELF): apps/editor_nano/editor_nano.c apps/editor_nano/font.h $(CRT0_OBJ) $(LIBGLOSS_A)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/editor_nano/editor_nano.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(NANO_ELF): apps/kilo/kilo.c $(CRT0_OBJ) $(LIBGLOSS_A) $(LIBC_A) $(LIBM_A)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/kilo/kilo.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(CRUN_ELF): apps/c4/c4.c $(CRT0_OBJ) $(LIBGLOSS_A)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/c4/c4.c -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

$(CALC_ELF): apps/calc/calc.c $(CRT0_OBJ) $(LIBGLOSS_A) sdk/lib/libliwus_gui.a
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/calc/calc.c -L$(NEWLIB_DIR) -Lsdk/lib -lliwus_gui -lgloss -lc -lm -o $@ $(LIBGCC)

TCC_DIR = third_party/tcc
TCC_CFLAGS = $(USER_CFLAGS) -I$(TCC_DIR) -DONE_SOURCE=1 -DTCC_TARGET_X86_64 \
             -DCONFIG_TCCDIR=\"/house/localhost/tccsdk\" -DCONFIG_TCC_SEMLOCK=0 \
             -DCONFIG_TCC_BACKTRACE=0 -DCONFIG_TCC_BCHECK=0 \
             -DCONFIG_TCC_LIBPATHS=\"/house/localhost/tccsdk/lib\" \
             -DCONFIG_TCC_CRTPREFIX=\"/house/localhost/tccsdk/lib\" \
             -DCONFIG_TCC_ELFINTERP=\"-\"

$(TCC_ELF): apps/tcc/tcc.c $(CRT0_OBJ) $(LIBGLOSS_A) $(LIBC_A) $(LIBM_A)
	@mkdir -p $(dir $@)
	$(CC) $(TCC_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/tcc/tcc.c -L$(NEWLIB_DIR) -lgloss -lc -lm -Wl,--allow-multiple-definition -o $@ $(LIBGCC)

# TCC e opcional: se a submodule '$(TCC_DIR)' nao estiver presente (ex.: apos
# o cleanup do repo), o app TCC e simplesmente omitido do build do ISO.
ifeq ($(wildcard $(TCC_DIR)/tcc.c),)
ISO_TCC_DEP =
else
ISO_TCC_DEP = $(TCC_ELF)
endif



sdk/lib/libliwus_gui.a: sdk/lib/liwus_gui.c
	$(CC) -c $< -o sdk/lib/liwus_gui.o $(USER_CFLAGS) -I$(CURDIR)/sdk/include
	$(AR) rcs $@ sdk/lib/liwus_gui.o

$(DEMO_GUI_ELF): apps/demo_gui/demo_gui.c $(CRT0_OBJ) $(LIBGLOSS_A) sdk/lib/libliwus_gui.a
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) apps/demo_gui/demo_gui.c -L$(NEWLIB_DIR) -Lsdk/lib -lliwus_gui -lgloss -lc -lm -o $@ $(LIBGCC)

$(LDE_ELF): lde/src/main.c lde/src/system_bridge.c $(CRT0_OBJ) $(LIBGLOSS_A) sdk/lib/libliwus_gui.a
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib -static $(CRT0_OBJ) lde/src/main.c lde/src/system_bridge.c -L$(NEWLIB_DIR) -Lsdk/lib -lliwus_gui -lgloss -lc -lm -o $@ $(LIBGCC)

$(ISO_IMAGE): $(KERNEL_BIN) $(BOOT_DIR)/test.elf $(DEMO_GUI_ELF) $(LDE_ELF) $(ISO_TCC_DEP)
	$(HOSTCC) -Iinclude -Iinclude/uapi sdk/tools/liw-builder.c -o sdk/tools/liw-builder
	$(HOSTCC) sdk/tools/img-gen.c -o sdk/tools/img-gen
	./sdk/tools/liw-builder src/boot/test.liw src/boot/test.elf src/boot/test_manifest.json
	grub-file --is-x86-multiboot2 $(KERNEL_BIN)
	mkdir -p repo
	./sdk/tools/img-gen
	if [ -f $(DEMO_GUI_ELF) ]; then cp $(DEMO_GUI_ELF) repo/demo_gui; fi
	if [ -f $(LDE_ELF) ]; then cp $(LDE_ELF) repo/lde; fi
	if [ -f $(TCC_ELF) ]; then cp $(TCC_ELF) repo/tcc; fi

	# Instala o SDK do TCC dentro do initrd (CONFIG_TCCDIR=/tccsdk).
	# estrutura: /tccsdk/include/*.h e /tccsdk/lib/libtcc1.a + libc/libm/gloss
	mkdir -p repo/tccsdk/lib repo/tccsdk/include
	if [ -f $(TCC_DIR)/tcclib.h ]; then cp $(TCC_DIR)/tcclib.h repo/tccsdk/include/; fi
	cp $(TCC_DIR)/include/*.h repo/tccsdk/include/ 2>/dev/null || true
	cp -r sdk/include/. repo/tccsdk/include/ 2>/dev/null || true
	if [ -f $(TCC_DIR)/lib/libtcc1.a ]; then cp $(TCC_DIR)/lib/libtcc1.a repo/tccsdk/lib/; fi
	if [ -f $(TCC_DIR)/libtcc1.a ]; then cp $(TCC_DIR)/libtcc1.a repo/tccsdk/lib/; fi
	cp $(LIBC_A) $(LIBM_A) $(LIBGLOSS_A) libgloss/crt0.o repo/tccsdk/lib/ 2>/dev/null || true
	# Objetos CRT exigidos pelo TCC ao linkar executáveis (crt1 = crt0)
	cp libgloss/crt0.o repo/tccsdk/lib/crt1.o 2>/dev/null || true
	$(MAKE) $(CRTI_OBJ) $(CRTN_OBJ)
	cp $(CRTI_OBJ) $(CRTN_OBJ) repo/tccsdk/lib/ 2>/dev/null || true

	tar -cvf initrd.tar -C repo . --format=ustar
	mkdir -p isodir/boot/grub
	cp $(KERNEL_BIN) isodir/boot/kernel.bin
	cp $(BOOT_DIR)/grub.cfg isodir/boot/grub/grub.cfg
	cp initrd.tar isodir/boot/initrd.tar
	# Create SDFS disk image for persistent storage (only if not exists)
	if [ ! -f liwus_disk.img ]; then dd if=/dev/zero of=liwus_disk.img bs=1M count=64 2>/dev/null; fi
	grub-mkrescue -o $(ISO_IMAGE) isodir

# ============================================================
# UEFI / hardware moderno (ver ROADMAP.md, Fase 0)
# ============================================================

# ISO com boot UEFI (OVMF e hardware real). Ver scripts/make_uefi_iso.sh.
uefi-iso: $(ISO_IMAGE)
	bash scripts/make_uefi_iso.sh liwusos-uefi.iso

# Testa o boot UEFI headless em QEMU + OVMF.
uefi-check: uefi-iso
	bash scripts/uefi_boot_check.sh liwusos-uefi.iso


# ============================================================
# Test ELF
# ============================================================

$(BOOT_DIR)/test.elf: $(BOOT_DIR)/test.s
	$(CC) -nostdlib -static $< -o $@ -m32 -Ttext 0x100000

# ---- Kernel-side test objects (SDFS tests) ----
$(OBJ_DIR)/$(TEST_DIR)/%.o: $(TEST_DIR)/%.c $(TEST_FW)
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(KERNEL_TEST_CFLAGS)

# ---- Userspace test runner ----
$(TEST_USER_ELF): $(USER_TEST_SRCS) $(CRT0_OBJ) $(LIBGLOSS_A)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -I$(TEST_DIR) -nostdlib -static $(CRT0_OBJ) \
	    $(USER_TEST_SRCS) -L$(NEWLIB_DIR) -lgloss -lc -lm -o $@ $(LIBGCC)

# ---- Test targets ----
test-sdfs: $(KERNEL_BIN)
	bash scripts/run_tests.sh --sdfs

test-net: $(KERNEL_BIN)
	bash scripts/run_tests.sh --net

test-user: $(KERNEL_BIN) $(TEST_USER_ELF)
	bash scripts/run_tests.sh --user

test: $(KERNEL_BIN) $(TEST_USER_ELF)
	bash scripts/run_tests.sh --full

test-clean:
	rm -f $(TEST_USER_ELF)
	rm -f $(KERNEL_TEST_OBJS)

# Atalho de compatibilidade: KVM=1 continua funcionando.
KVM_FLAGS = $(if $(filter 1,$(KVM)),-enable-kvm,)

# ============================================================
# HARDWARE DE TESTE UNIVERSAL - PC Bay Trail / Celeron J1800
# ============================================================
# TODO alvo de execucao (run/run-serial/run-log/run-lan/run-bios) emula o
# PC real do projeto, que e tambem o alvo de hardware fisico:
#   CPU  : Celeron J1800 (Silvermont, 2C/2T)  -> KVM: -cpu host | TCG: -cpu max
#   RAM  : 2 GB DDR3-1333
#   Firm.: AMI UEFI 5.6.5 (2014)              -> OVMF (run-bios: SeaBIOS)
#   Disco: SATA AHCI (WD WD5000LPVX)          -> ahci + ide-hd
#   USB  : xHCI + EHCI                        -> qemu-xhci + usb-ehci
#   Audio: Intel HDA + Realtek ALC269VC       -> intel-hda + hda-duplex
#   Rede : RTL8168/8111 (r8169)               -> rtl8139 (QEMU nao emula r8169)
#   WiFi : RTL8188CE                          -> nao usado
#
# O QEMU NAO emula o RTL8168/r8169 nem a GPU Intel Gen7; usamos o rtl8139
# (Realtek, suportado pelo kernel). O r8169 so e validado no metal.
#
# Ajustes:  HW_MEM/HW_SMP   desliga KVM: HW_KVM=0   disco: DISK_DEV=nvme
#           audio: AUDIO_DEV=ac97   LAN real: make run-lan
# ============================================================

HW_MACHINE   ?= q35
HW_MEM       ?= 2048
HW_SMP       ?= 2
HW_KVM       ?= $(if $(shell test -w /dev/kvm && echo 1),1,)
HW_CPU       ?= $(if $(filter 1,$(HW_KVM)),host,max)
HW_KVM_FLAGS  = $(if $(filter 1,$(HW_KVM)),-enable-kvm,)

HW_OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
HW_OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
HW_VARS       = build/uefi_vars.fd
HW_UEFI_ISO  ?= liwusos-uefi.iso

HW_SMBIOS = \
	-smbios type=0,vendor="American Megatrends Inc.",version="5.6.5",date="05/13/2014" \
	-smbios type=1,manufacturer="AOC",product="BTDD-EAIO",version="1.0",serial="Default string" \
	-smbios type=4,manufacturer="Intel(R) Corporation",version="Intel(R) Celeron(R) CPU J1800 @ 2.41GHz",serial="To be filled by O.E.M." \
	-smbios type=17,manufacturer="Samsung",serial="Default string",part="DDR3",speed=1333

HW_FLAGS = \
	-machine $(HW_MACHINE) \
	-cpu $(HW_CPU) -smp $(HW_SMP) -m $(HW_MEM) \
	$(DISK_FLAGS) \
	-device qemu-xhci,id=xhci,p2=4,p3=4 \
	-device usb-ehci,id=ehci \
	-device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0 \
	$(NET_FLAGS) \
	$(AUDIO_FLAGS) \
	$(HW_SMBIOS)

HW_UEFI = \
	-drive if=pflash,format=raw,readonly=on,file=$(HW_OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(HW_VARS)

HW_AUDIO_ENV = PULSE_SERVER=$(if $(filter pa,$(AUDIO_BACKEND)),/mnt/wslg/PulseServer,) GDK_BACKEND=x11 SDL_VIDEODRIVER=x11

hw-prep:
	@mkdir -p build
	@if [ ! -f liwus_disk.img ]; then dd if=/dev/zero of=liwus_disk.img bs=1M count=64 2>/dev/null; fi
	@if [ ! -f $(HW_VARS) ]; then cp $(HW_OVMF_VARS) $(HW_VARS); fi

# Janela (UEFI/OVMF = firmware do PC real).
run: uefi-iso hw-prep
	$(HW_AUDIO_ENV) \
	qemu-system-x86_64 $(HW_KVM_FLAGS) $(HW_FLAGS) $(HW_UEFI) -cdrom $(HW_UEFI_ISO)

# Janela + serial no terminal + erros de guest.
run-serial: uefi-iso hw-prep
	$(HW_AUDIO_ENV) \
	qemu-system-x86_64 $(HW_KVM_FLAGS) $(HW_FLAGS) $(HW_UEFI) -cdrom $(HW_UEFI_ISO) -serial stdio -d guest_errors -no-reboot

# Headless: serial -> qemu_serial.log + depuracao de interrupcoes.
run-log: AUDIO_BACKEND = none
run-log: uefi-iso hw-prep
	qemu-system-x86_64 $(HW_KVM_FLAGS) $(HW_FLAGS) $(HW_UEFI) -cdrom $(HW_UEFI_ISO) \
		-display none -monitor none -serial file:qemu_serial.log -D qemu_debug.log -d int,cpu_reset -no-reboot

# Hardware universal com firmware legado (SeaBIOS) - comparacao/debug.
run-bios: $(ISO_IMAGE) hw-prep
	$(HW_AUDIO_ENV) \
	qemu-system-x86_64 $(HW_KVM_FLAGS) $(HW_FLAGS) -cdrom $(ISO_IMAGE)

# Roda o OS na LAN de VERDADE (placa TAP na rede local).
run-lan: uefi-iso
	bash scripts/run_lan.sh
	@$(MAKE) run NET_MODE=tap

# Aliases (mesmo hardware e firmware).
run-j1800: run
run-j1800-bios: run-bios
run-j1800-log: run-log

qa-boot-persistence: $(ISO_IMAGE)
	bash ./scripts/qa_boot_persistence.sh

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(KERNEL_BIN) $(ISO_IMAGE) liwusos-uefi.iso initrd.tar src/boot/test.elf src/boot/test.liw $(CALC_ELF) $(HELLO_ELF) $(DOOMPROBE_ELF) $(LUA_ELF) $(CRUN_ELF) $(EDITOR_NANO_ELF) $(DEMO_GUI_ELF) $(LDE_ELF) $(TCC_ELF) $(LIBGLOSS_A) $(LIBGLOSS_OBJS) $(CRT0_OBJ)
	rm -f $(TEST_USER_ELF) $(KERNEL_TEST_OBJS) test_serial.log test_disk.img
	rm -f repo/test_mode repo/test_runner
	@echo "  NOTA: liwus_disk.img preservado (remova manualmente se quiser disco limpo)"
	rm -f sdk/tools/liw-builder sdk/tools/img-gen
	rm -rf isodir repo
