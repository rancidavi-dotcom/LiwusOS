# ============================================================
# LiwusOS - build enxuto (kernel + SDFS + terminal)
# ============================================================

.DEFAULT_GOAL := all

CC       = gcc
LD       = gcc
OBJCOPY  = objcopy
GRUB     = grub-mkrescue
QEMU     = qemu-system-x86_64

M64 = -m64 -mno-red-zone

BEARSSL_DIR = src/net/bearssl
BEARSSL_LIB = $(BEARSSL_DIR)/build/libbearssl.a

LVGL_DIR = src/kernel/gui/lvgl
LVGL_LIB = $(LVGL_DIR)/liblvgl.a

KERNEL_INCLUDES = -Iinclude -Iinclude/kernel -Iinclude/drivers -Iinclude/fs \
                  -Iinclude/uapi -Iinclude/net -Iinclude/gui -Isrc/kernel/gui \
                  -Iinclude/libc \
                  -I$(LVGL_DIR) -DLV_CONF_INCLUDE_SIMPLE \
                  -Isrc/net -Isrc/net/lwip/include -I$(BEARSSL_DIR)/inc \
                  -Isrc/kernel -Isrc/kernel/terminal -Itests

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Werror -MMD -MP $(KERNEL_INCLUDES) $(M64) \
         -fno-pie -fno-pic -mcmodel=large -mno-sse -mno-sse2 -mno-mmx \
         -fno-stack-protector -fno-asynchronous-unwind-tables

LDFLAGS = -Wl,-no-pie
LIBGCC  = -lgcc

OBJ_DIR = obj
TEST_OBJ_DIR = obj-test

BOOT_DIR = src/boot
KERNEL_BIN = kernel.bin
TEST_KERNEL_BIN = kernel-test.bin
ISO_IMAGE = liwusos.iso
TEST_ISO_IMAGE = liwusos-test.iso

# The host checkout owner must never determine permissions of installed system
# files. User-editable examples opt into UID/GID 1000 on their tar entries.
export TAR_OPTIONS := --owner=0 --group=0 --numeric-owner
UPDATE_FEED_DIR ?= build/update-feed
UPDATE_VERSION ?= 2.2.13
UPDATE_DATE ?= 2026-11-13
UPDATE_BASE_URL ?= https://raw.githubusercontent.com/rancidavi-dotcom/LiwusOS/pkg-repo
UPDATE_NOTES ?= LiwusWeb: APIs basicas, modulos inline e CSS nth-child
LINUX_STATIC_TEST_DEP =
LINUX_STATIC_EXPECT ?= Hello from an unmodified static GNU/Linux ELF
ifneq ($(strip $(LINUX_STATIC_ELF)),)
LINUX_STATIC_TEST_DEP = $(LINUX_STATIC_ELF)
endif
DISK_IMAGE = liwus_disk.img

BOOT_SRCS = $(BOOT_DIR)/boot.s $(BOOT_DIR)/interrupt.s

KERNEL_C_SRCS = $(wildcard src/kernel/arch/x86_64/*.c) \
                $(wildcard src/kernel/core/*.c) \
                $(wildcard src/compat/linux/*.c) \
                $(wildcard src/kernel/lib/*.c) \
                $(wildcard src/kernel/mm/*.c) \
                $(wildcard src/kernel/sched/*.c) \
                $(wildcard src/kernel/terminal/*.c) \
                $(wildcard src/kernel/gui/*.c) \
                $(wildcard src/drivers/*.c) \
                $(wildcard src/fs/*.c) \
                $(wildcard src/net/*.c) \
                $(wildcard src/net/trust/*.c) \
                $(wildcard src/net/arch/*.c) \
                $(wildcard src/net/lwip/core/*.c) \
                $(wildcard src/net/lwip/core/ipv4/*.c) \
                $(wildcard src/net/lwip/netif/*.c)

KERNEL_S_SRCS = $(wildcard src/kernel/arch/x86_64/*.s) \
                $(wildcard src/kernel/lib/*.s) \
                $(BOOT_SRCS)

KERNEL_C_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_S_OBJS = $(patsubst %.s,$(OBJ_DIR)/%.o,$(KERNEL_S_SRCS))
KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_S_OBJS) $(OBJ_DIR)/src/drivers/font.o

TEST_SRCS = tests/test_runner_kernel.c \
            tests/test_terminal.c \
            tests/test_sdfs_create.c \
            tests/test_sdfs_rw.c \
            tests/test_sdfs_dir.c \
            tests/test_sdfs_rename.c \
            tests/test_sdfs_delete.c \
            tests/test_sdfs_persist.c \
            tests/test_sdfs_diskfull.c \
            tests/test_sdfs_crc32.c \
            tests/test_sdfs_journal.c \
            tests/test_sdfs_perms.c \
            tests/test_sdfs_v2.c \
            tests/test_sched.c \
            tests/test_vma.c \
            tests/test_pmm_high.c \
            tests/test_pmm_contract.c \
            tests/test_kheap.c \
            tests/test_kstack.c \
            tests/test_process_model.c \
            tests/test_runner_ring3.c

TEST_OBJS = $(patsubst %.c,$(TEST_OBJ_DIR)/%.o,$(TEST_SRCS))
TEST_S_OBJS = $(TEST_OBJ_DIR)/tests/kstack_fault_probe.o
# Header changes must rebuild users of shared kernel/process layouts.
-include $(KERNEL_C_OBJS:.o=.d) $(TEST_OBJS:.o=.d) \
	$(TEST_OBJ_DIR)/src/kernel/core/kernel.d $(TEST_OBJ_DIR)/src/fs/sdfs.d
# Kernel compilado com -DKERNEL_TEST (substitui kernel.o e SDFS.o normais)
TEST_KERNEL_OBJS = $(filter-out $(OBJ_DIR)/src/kernel/core/kernel.o \
	$(OBJ_DIR)/src/fs/sdfs.o,$(KERNEL_OBJS)) \
                   $(TEST_OBJ_DIR)/src/kernel/core/kernel.o \
                   $(TEST_OBJ_DIR)/src/fs/sdfs.o \
                   $(TEST_OBJS) $(TEST_S_OBJS)

TEST_CFLAGS = $(CFLAGS) -DKERNEL_TEST
RING3_USER_ELF = $(TEST_OBJ_DIR)/ring3/suite.elf
RING3_EXEC_ELF = $(TEST_OBJ_DIR)/ring3/exec_target.elf
RING3_C_APP_ELF = $(TEST_OBJ_DIR)/ring3/c_example.elf
RING3_PIE_ELF = $(TEST_OBJ_DIR)/ring3/pie_probe.elf
RING3_LINUX_SYSCALL_ELF = $(TEST_OBJ_DIR)/ring3/linux_syscall_probe.elf
RING3_LINUX_CLONE_ELF = $(TEST_OBJ_DIR)/ring3/linux_clone_probe.elf
RING3_PERMISSION_ELF = $(TEST_OBJ_DIR)/ring3/permission_probe.elf
RING3_WEBJS_ELF = build/liwus-sdk/bin/qjs_pipe_probe.lxe
RING3_USER_CFLAGS = -std=gnu99 -O2 -ffreestanding -fno-pie -fno-pic \
	-fno-stack-protector -fno-asynchronous-unwind-tables -m64 -mno-red-zone \
	-Iinclude/uapi
USER_SHELL_ELF = $(OBJ_DIR)/programs/user_shell.elf
USER_SHELL_CFLAGS = $(RING3_USER_CFLAGS) -Iprograms/c_example
USER_INIT_ELF = $(OBJ_DIR)/programs/user_init.elf
USER_EDITOR_ELF = $(OBJ_DIR)/programs/user_editor.elf
NATIVE_HELLO_LXE = build/liwus-sdk/bin/native-hello.lxe
LIWSH_LXE = build/liwus-sdk/bin/liwsh.lxe
LS_LXE = build/liwus-sdk/bin/ls.lxe
CAT_LXE = build/liwus-sdk/bin/cat.lxe
MKDIR_LXE = build/liwus-sdk/bin/mkdir.lxe
RM_LXE = build/liwus-sdk/bin/rm.lxe
MV_LXE = build/liwus-sdk/bin/mv.lxe
CP_LXE = build/liwus-sdk/bin/cp.lxe
TOUCH_LXE = build/liwus-sdk/bin/touch.lxe
ECHO_LXE = build/liwus-sdk/bin/echo.lxe
RMDIR_LXE = build/liwus-sdk/bin/rmdir.lxe
STAT_LXE = build/liwus-sdk/bin/stat.lxe
HEAD_LXE = build/liwus-sdk/bin/head.lxe
WEBTEXT_LXE = build/liwus-sdk/bin/webtext.lxe
QJS_LXE = build/liwus-sdk/bin/qjs.lxe
C_DEMO_LXE = build/liwus-sdk/bin/c_demo.lxe
TCC_LXE = build/liwus-sdk/bin/tcc.lxe
KILO_LXE = build/liwus-sdk/bin/kilo.lxe
DOOM_LXE = build/liwus-sdk/bin/doom.lxe
NEOFETCH_LXE = build/liwus-sdk/bin/neofetch.lxe
SNAKE_LXE = build/liwus-sdk/bin/snake.lxe
MATRIX_LXE = build/liwus-sdk/bin/matrix.lxe
SYSBENCH_LXE = build/liwus-sdk/bin/sysbench.lxe
PKG_REPO_LXES = $(NEOFETCH_LXE) $(SNAKE_LXE) $(MATRIX_LXE) $(SYSBENCH_LXE)
NATIVE_UTIL_LXES = $(MKDIR_LXE) $(RM_LXE) $(MV_LXE) $(CP_LXE) \
	$(TOUCH_LXE) $(ECHO_LXE) $(RMDIR_LXE) $(STAT_LXE) $(HEAD_LXE) $(WEBTEXT_LXE) $(QJS_LXE) $(C_DEMO_LXE) $(TCC_LXE) $(KILO_LXE) $(DOOM_LXE)
EXTERNAL_SDK_TEST_LXE = build/external-test.lxe
SDK_LIBRARY = build/liwus-sdk/lib/libliwus.a
SDK_OPENLIBM = build/liwus-sdk/lib/libopenlibm.a
SDK_CRT0 = build/liwus-sdk/lib/crt0.o
SDK_LINKER_SCRIPT = build/liwus-sdk/share/liwus/elf64.ld
SDK_HEADERS_STAMP = build/liwus-sdk/include/.headers-installed

.PHONY: all run run-log test test-build ring3-test smap-test user-c-example sdk sdk-install liwsh external-sdk-build shell-test app-install sdfs-put clean
.PHONY: test-harness test-unit test-stress test-all app-install-test
.NOTPARALLEL: test-all sdk shell-test app-install-test

user-c-example: $(RING3_C_APP_ELF)

sdk: $(NATIVE_HELLO_LXE) $(LIWSH_LXE) $(LS_LXE) $(CAT_LXE) \
	$(NATIVE_UTIL_LXES) $(PKG_REPO_LXES) $(SDK_LIBRARY) $(SDK_CRT0) \
	$(SDK_HEADERS_STAMP) $(SDK_LINKER_SCRIPT)

# Install only the public SDK artifacts. SDK_PREFIX is deliberately explicit
# so callers can use an isolated prefix (including one outside this checkout).
SDK_PREFIX ?= $(CURDIR)/build/liwus-sdk-install
sdk-install: sdk
	@test -n "$(SDK_PREFIX)" || { echo "SDK_PREFIX must not be empty" >&2; exit 2; }
	@mkdir -p "$(SDK_PREFIX)/include" "$(SDK_PREFIX)/lib" "$(SDK_PREFIX)/share/liwus"
	@cp -R build/liwus-sdk/include/liwus "$(SDK_PREFIX)/include/"
	@install -m 0644 $(SDK_LIBRARY) "$(SDK_PREFIX)/lib/libliwus.a"
	@install -m 0644 $(SDK_CRT0) "$(SDK_PREFIX)/lib/crt0.o"
	@install -m 0644 $(SDK_LINKER_SCRIPT) "$(SDK_PREFIX)/share/liwus/elf64.ld"

liwsh: sdk $(LIWSH_LXE)

$(LS_LXE): programs/ls/main.c programs/ls/Makefile $(SDK_LIBRARY) \
		$(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/ls all

$(CAT_LXE): programs/cat/main.c programs/cat/Makefile $(SDK_LIBRARY) \
		$(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/cat all

$(MKDIR_LXE): programs/mkdir/main.c programs/mkdir/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/mkdir all

$(RM_LXE): programs/rm/main.c programs/rm/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/rm all

$(MV_LXE): programs/mv/main.c programs/mv/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/mv all

$(CP_LXE): programs/cp/main.c programs/cp/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/cp all

$(TOUCH_LXE): programs/touch/main.c programs/touch/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/touch all

$(ECHO_LXE): programs/echo/main.c programs/echo/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/echo all

$(RMDIR_LXE): programs/rmdir/main.c programs/rmdir/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/rmdir all

$(STAT_LXE): programs/stat/main.c programs/stat/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/stat all

$(HEAD_LXE): programs/head/main.c programs/head/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/head all

$(WEBTEXT_LXE): programs/webtext/main.c programs/webtext/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/webtext all

$(SDK_OPENLIBM): third_party/openlibm/port/Makefile \
		$(wildcard third_party/openlibm/src/*.c) \
		$(wildcard third_party/openlibm/bsdsrc/*.c) \
		third_party/openlibm/amd64/fenv.c \
		$(wildcard third_party/openlibm/src/*.h) \
		$(wildcard third_party/openlibm/include/*.h)
	$(MAKE) -C third_party/openlibm/port all

$(QJS_LXE): programs/qjs/main.c programs/qjs/Makefile \
		$(wildcard third_party/quickjs/*.c) \
		$(wildcard third_party/quickjs/*.h) \
		$(wildcard third_party/quickjs/*.inc) \
		$(SDK_LIBRARY) $(SDK_OPENLIBM) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/qjs all

$(C_DEMO_LXE): programs/c_demo/main.c programs/c_demo/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/c_demo all

$(TCC_LXE): $(wildcard programs/tcc/*.c) $(wildcard programs/tcc/*.h) programs/tcc/Makefile $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/tcc all

$(KILO_LXE): $(wildcard programs/kilo/*.c) programs/kilo/Makefile $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/kilo all

$(DOOM_LXE): $(wildcard programs/doom/*.c) $(wildcard programs/doom/*.h) programs/doom/Makefile $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -j$(shell nproc) -C programs/doom all

$(NEOFETCH_LXE): programs/neofetch/main.c programs/neofetch/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/neofetch all

$(SNAKE_LXE): programs/snake/main.c programs/snake/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/snake all

$(MATRIX_LXE): programs/matrix/main.c programs/matrix/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/matrix all

$(SYSBENCH_LXE): programs/sysbench/main.c programs/sysbench/Makefile programs/native-app.mk $(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/sysbench all

$(LIWSH_LXE): programs/liwsh/main.c programs/liwsh/parser.c \
		programs/liwsh/parser.h programs/liwsh/Makefile $(SDK_LIBRARY) \
		$(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	$(MAKE) -C programs/liwsh all

external-sdk-build: $(EXTERNAL_SDK_TEST_LXE)

shell-test: all external-sdk-build test-unit
	python3 tests/harness/run_shell_integration.py

# Offline installer regressions: host SDFS/ELF cases plus one temporary disk
# reused across first boot, install, update, and later boots. Never touches
# liwus_disk.img.
app-install-test: all test-unit
	python3 tests/install/test_sdfs_installer.py
	python3 tests/install/run_persistence_test.py

app-install: sdk
	@test -n "$(APP)" || { echo "usage: make app-install APP=program.lxe DISK=existing-sdfs.img [REPLACE=1]" >&2; exit 2; }
	@test -n "$(DISK)" || { echo "DISK must name an existing offline SDFS image" >&2; exit 2; }
	python3 tools/liwus_sdfs_install.py --app "$(APP)" --disk "$(DISK)" $(if $(filter 1 yes true,$(REPLACE)),--replace,)

# Explicit offline import for regular data files (for example README updates).
sdfs-put:
	@test -n "$(FILE)" -a -n "$(DEST)" -a -n "$(DISK)" || { echo "usage: make sdfs-put FILE=... DEST=/absolute/destination DISK=existing-sdfs.img [REPLACE=1]" >&2; exit 2; }
	python3 tools/liwus_sdfs_install.py --file "$(FILE)" --path "$(DEST)" --disk "$(DISK)" $(if $(filter 1 yes true,$(REPLACE)),--replace,)

$(EXTERNAL_SDK_TEST_LXE): tests/external_sdk/build_external.py \
		$(SDK_LIBRARY) $(SDK_CRT0) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	python3 tests/external_sdk/build_external.py --output $@

lib/liwus/libliwus.a: lib/liwus/Makefile \
		$(wildcard lib/liwus/*.c lib/liwus/*.S) lib/liwus/webdom_entities.inc \
		scripts/gen_webdom_entities.py $(wildcard include/liwus/*.h) include/uapi/syscall_nums.h
	$(MAKE) -C lib/liwus all

lib/liwus/crt0.o: lib/liwus/Makefile lib/liwus/crt0.S
	$(MAKE) -C lib/liwus crt0.o

$(NATIVE_HELLO_LXE): programs/native-hello/main.c \
		sdk/elf64.ld programs/native-hello/Makefile \
		lib/liwus/syscall.c lib/liwus/crt.c lib/liwus/crt0.S \
		lib/liwus/string.c lib/liwus/Makefile \
		include/liwus/liwus.h include/liwus/syscall.h include/liwus/stat.h \
		include/liwus/string.h include/liwus/fcntl.h include/liwus/mman.h \
		include/uapi/syscall_nums.h
	$(MAKE) -C programs/native-hello lxe

$(SDK_LIBRARY): lib/liwus/libliwus.a
	@mkdir -p $(dir $@)
	cp -f $< $@
	cp -f $< $(dir $@)/libc.a
	cp -f $< $(dir $@)/libtcc1.a

$(SDK_CRT0): lib/liwus/crt0.o
	@mkdir -p $(dir $@)
	cp -f $< $@
	cp -f $< $(dir $@)/crt1.o
	@echo "" | as --64 -o $(dir $@)/crti.o
	@echo "" | as --64 -o $(dir $@)/crtn.o

$(SDK_LINKER_SCRIPT): sdk/elf64.ld
	@mkdir -p $(dir $@)
	cp -f $< $@

$(SDK_HEADERS_STAMP): $(wildcard include/liwus/*.h) $(wildcard include/libc/*.h) $(wildcard include/libc/sys/*.h)
	@mkdir -p $(dir $@)
	cp -R include/liwus build/liwus-sdk/include/
	cp -R include/libc/* build/liwus-sdk/include/
	touch $@

ring3-test: test-unit $(TEST_ISO_IMAGE)
	@mkdir -p test-logs
	@serial_log=$$(mktemp test-logs/ring3-XXXXXX.serial); \
	timeout 90 $(QEMU) -machine q35 -m 2048 -display none -monitor none \
		-serial file:$$serial_log -no-reboot \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-cdrom $(TEST_ISO_IMAGE); qemu_rc=$$?; \
	python3 tests/harness/validate_serial.py --mode ring3 \
		--qemu-exit $$qemu_rc --log $$serial_log \
		--linux-output "$(LINUX_STATIC_EXPECT)"; check_rc=$$?; \
	cp -f $$serial_log /tmp/liwusos-ring3-integration.serial; \
	exit $$check_rc

# Exercise usercopy, stack guards, and high physical RAM on SMEP/SMAP CPUs.
smap-test: test-unit $(TEST_ISO_IMAGE)
	@mkdir -p test-logs
	@serial_log=$$(mktemp test-logs/smap-XXXXXX.serial); \
	timeout 90 $(QEMU) -machine q35 -cpu max -smp 4 -m 4096 \
		-display none -monitor none \
		-serial file:$$serial_log \
		-no-reboot \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-drive file=$(DISK_IMAGE),format=raw,if=none,id=disk \
		-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
		-cdrom $(TEST_ISO_IMAGE); qemu_rc=$$?; \
	python3 tests/harness/validate_serial.py --mode smap \
		--qemu-exit $$qemu_rc --log $$serial_log \
		--linux-output "$(LINUX_STATIC_EXPECT)"; check_rc=$$?; \
	cp -f $$serial_log /tmp/liwusos-smap-integration.serial; \
	exit $$check_rc

all: $(KERNEL_BIN) $(ISO_IMAGE)

# ------------------------------------------------------------
# Kernel
# ------------------------------------------------------------

$(BEARSSL_LIB):
	@mkdir -p $(BEARSSL_DIR)/build/obj
	$(MAKE) -C $(BEARSSL_DIR) lib CC="$(CC)" CFLAGS="-std=gnu99 -ffreestanding -O2 -Wall -Wextra $(M64) -fno-pie -fno-pic -mcmodel=large -mno-sse -mno-sse2 -mno-mmx -fno-stack-protector -fno-asynchronous-unwind-tables -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=1 -Iinc -Isrc"

$(LVGL_LIB):
	$(MAKE) -C $(LVGL_DIR) CC="$(CC)" AR="$(AR)" M64="$(M64)"

$(KERNEL_BIN): $(KERNEL_OBJS) $(BEARSSL_LIB) $(LVGL_LIB) $(BOOT_DIR)/linker.ld
	$(LD) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) $(LDFLAGS) \
		-nostdlib -static -Wl,--build-id=none $(KERNEL_OBJS) $(BEARSSL_LIB) $(LVGL_LIB) $(LIBGCC)

$(TEST_KERNEL_BIN): $(TEST_KERNEL_OBJS) $(BEARSSL_LIB) $(LVGL_LIB) $(BOOT_DIR)/linker.ld
	$(LD) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) $(LDFLAGS) \
		-nostdlib -static -Wl,--build-id=none $(TEST_KERNEL_OBJS) $(BEARSSL_LIB) $(LVGL_LIB) $(LIBGCC)

$(OBJ_DIR)/src/kernel/gui/%.o: src/kernel/gui/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(filter-out -mno-sse -mno-sse2 -mno-mmx -Werror,$(CFLAGS)) -msse -msse2 -Wno-unused-function -Wno-unused-parameter

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

$(OBJ_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(M64)

$(OBJ_DIR)/src/drivers/font.o: src/drivers/font.psf
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

# kernel de teste: mesmo codigo, mas com -DKERNEL_TEST
$(TEST_OBJ_DIR)/src/kernel/core/kernel.o: src/kernel/core/kernel.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(TEST_CFLAGS)

$(TEST_OBJ_DIR)/src/fs/sdfs.o: src/fs/sdfs.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(TEST_CFLAGS)

$(TEST_OBJ_DIR)/tests/%.o: tests/%.c tests/framework.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(TEST_CFLAGS)

$(TEST_OBJ_DIR)/tests/%.o: tests/%.s
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(M64)

$(TEST_OBJ_DIR)/ring3/suite.o: tests/ring3/suite.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(RING3_USER_CFLAGS)

$(RING3_USER_ELF): $(TEST_OBJ_DIR)/ring3/suite.o tests/ring3/suite.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,tests/ring3/suite.ld -o $@ $(TEST_OBJ_DIR)/ring3/suite.o

$(TEST_OBJ_DIR)/ring3/pie_probe.o: tests/ring3/pie_probe.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fpie -fno-pic

$(RING3_PIE_ELF): $(TEST_OBJ_DIR)/ring3/pie_probe.o
	$(LD) -nostdlib -pie -Wl,--no-dynamic-linker -Wl,--build-id=none \
		-Wl,-e,_start -o $@ $<

$(TEST_OBJ_DIR)/ring3/linux_syscall_probe.o: tests/ring3/linux_syscall_probe.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fno-pie -fno-pic

$(RING3_LINUX_SYSCALL_ELF): $(TEST_OBJ_DIR)/ring3/linux_syscall_probe.o tests/ring3/linux_probe.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,tests/ring3/linux_probe.ld -o $@ $<

$(TEST_OBJ_DIR)/ring3/linux_clone_probe.o: tests/ring3/linux_clone_probe.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fno-pie -fno-pic

$(RING3_LINUX_CLONE_ELF): $(TEST_OBJ_DIR)/ring3/linux_clone_probe.o tests/ring3/suite.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,tests/ring3/suite.ld -o $@ $<

$(TEST_OBJ_DIR)/ring3/permission_probe.o: tests/ring3/permission_probe.c $(SDK_HEADERS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu99 -O2 -ffreestanding -fno-builtin -fno-pie -fno-pic \
		-fno-stack-protector -fno-asynchronous-unwind-tables -m64 -mno-red-zone \
		-Ibuild/liwus-sdk/include -c $< -o $@

$(RING3_PERMISSION_ELF): $(TEST_OBJ_DIR)/ring3/permission_probe.o $(SDK_CRT0) $(SDK_LIBRARY) $(SDK_LINKER_SCRIPT)
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,$(SDK_LINKER_SCRIPT) -o $@ $(SDK_CRT0) \
		$(TEST_OBJ_DIR)/ring3/permission_probe.o $(SDK_LIBRARY)

$(RING3_WEBJS_ELF): tests/ring3/webjs_probe.c $(SDK_CRT0) $(SDK_LIBRARY) $(SDK_LINKER_SCRIPT) $(SDK_HEADERS_STAMP)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(TEST_OBJ_DIR)/ring3/webjs_probe.o)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
		-fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
		-Ibuild/liwus-sdk/include -c $< -o $(TEST_OBJ_DIR)/ring3/webjs_probe.o
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,$(SDK_LINKER_SCRIPT) -o $@ $(SDK_CRT0) \
		$(TEST_OBJ_DIR)/ring3/webjs_probe.o $(SDK_LIBRARY)

$(TEST_OBJ_DIR)/ring3/exec_target.o: tests/ring3/exec_target.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(RING3_USER_CFLAGS)

$(TEST_OBJ_DIR)/ring3/exec_target_entry.o: tests/ring3/exec_target.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fno-pie -fno-pic

$(RING3_EXEC_ELF): $(TEST_OBJ_DIR)/ring3/exec_target.o $(TEST_OBJ_DIR)/ring3/exec_target_entry.o tests/ring3/exec_target.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,tests/ring3/exec_target.ld -o $@ \
		$(TEST_OBJ_DIR)/ring3/exec_target_entry.o $(TEST_OBJ_DIR)/ring3/exec_target.o

$(TEST_OBJ_DIR)/ring3/c_example.o: programs/c_example/main.c programs/c_example/liwus.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(RING3_USER_CFLAGS)

$(TEST_OBJ_DIR)/ring3/c_runtime.o: programs/c_example/runtime.c programs/c_example/liwus.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(RING3_USER_CFLAGS)

$(TEST_OBJ_DIR)/ring3/c_start.o: programs/c_example/start.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fno-pie -fno-pic

$(RING3_C_APP_ELF): $(TEST_OBJ_DIR)/ring3/c_example.o $(TEST_OBJ_DIR)/ring3/c_runtime.o $(TEST_OBJ_DIR)/ring3/c_start.o programs/c_example/linker.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,programs/c_example/linker.ld -o $@ \
		$(TEST_OBJ_DIR)/ring3/c_start.o $(TEST_OBJ_DIR)/ring3/c_runtime.o $(TEST_OBJ_DIR)/ring3/c_example.o

$(OBJ_DIR)/programs/user_shell/main.o: programs/user_shell/main.c programs/c_example/liwus.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_SHELL_CFLAGS)

$(OBJ_DIR)/programs/user_shell/runtime.o: programs/c_example/runtime.c programs/c_example/liwus.h include/uapi/syscall_nums.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_SHELL_CFLAGS)

$(OBJ_DIR)/programs/user_shell/start.o: programs/c_example/start.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fno-pie -fno-pic

$(USER_SHELL_ELF): $(OBJ_DIR)/programs/user_shell/main.o $(OBJ_DIR)/programs/user_shell/runtime.o $(OBJ_DIR)/programs/user_shell/start.o programs/user_shell/linker.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,programs/user_shell/linker.ld -o $@ \
		$(OBJ_DIR)/programs/user_shell/start.o $(OBJ_DIR)/programs/user_shell/runtime.o $(OBJ_DIR)/programs/user_shell/main.o

# ------------------------------------------------------------
# Imagens
# ------------------------------------------------------------

$(DISK_IMAGE):
	@if [ -e "$@" ]; then \
		echo "$@ already exists; preserving persistent disk image"; \
	else \
		dd if=/dev/zero of=$@ bs=1M count=64 status=none; \
	fi

isodir/boot/kernel.bin: $(KERNEL_BIN)
	@mkdir -p isodir/boot/grub
	cp $(KERNEL_BIN) $@

isodir/boot/grub/grub.cfg: $(BOOT_DIR)/grub.cfg
	@mkdir -p $(dir $@)
	cp $< $@

$(OBJ_DIR)/programs/user_init/main.o: programs/user_init/main.c programs/c_example/liwus.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_SHELL_CFLAGS)

$(OBJ_DIR)/programs/user_init/runtime.o: programs/c_example/runtime.c programs/c_example/liwus.h include/uapi/syscall_nums.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_SHELL_CFLAGS)

$(OBJ_DIR)/programs/user_init/start.o: programs/c_example/start.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fno-pie -fno-pic

$(USER_INIT_ELF): $(OBJ_DIR)/programs/user_init/main.o $(OBJ_DIR)/programs/user_init/runtime.o $(OBJ_DIR)/programs/user_init/start.o programs/user_init/linker.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,programs/user_init/linker.ld -o $@ \
		$(OBJ_DIR)/programs/user_init/start.o $(OBJ_DIR)/programs/user_init/runtime.o $(OBJ_DIR)/programs/user_init/main.o

$(OBJ_DIR)/programs/user_editor/main.o: programs/user_editor/main.c programs/c_example/liwus.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_SHELL_CFLAGS)

$(OBJ_DIR)/programs/user_editor/runtime.o: programs/c_example/runtime.c programs/c_example/liwus.h include/uapi/syscall_nums.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_SHELL_CFLAGS)

$(OBJ_DIR)/programs/user_editor/start.o: programs/c_example/start.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -m64 -fno-pie -fno-pic

$(USER_EDITOR_ELF): $(OBJ_DIR)/programs/user_editor/main.o $(OBJ_DIR)/programs/user_editor/runtime.o $(OBJ_DIR)/programs/user_editor/start.o programs/user_editor/linker.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,programs/user_editor/linker.ld -o $@ \
		$(OBJ_DIR)/programs/user_editor/start.o $(OBJ_DIR)/programs/user_editor/runtime.o $(OBJ_DIR)/programs/user_editor/main.o

$(ISO_IMAGE): Makefile isodir/boot/kernel.bin isodir/boot/grub/grub.cfg $(DISK_IMAGE) $(USER_INIT_ELF) $(USER_EDITOR_ELF) $(RING3_C_APP_ELF) $(NATIVE_HELLO_LXE) $(LIWSH_LXE) $(LS_LXE) $(CAT_LXE) $(NATIVE_UTIL_LXES) $(PKG_REPO_LXES) $(KILO_LXE) $(DOOM_LXE) $(TCC_LXE) $(C_DEMO_LXE) assets/readme.txt assets/doom1.wad
	@mkdir -p repo build/hd_isodir/boot/grub
	cp $(KERNEL_BIN) build/hd_isodir/boot/kernel.bin
	printf 'set timeout=1\nset default=0\ninsmod all_video\ninsmod gfxterm\ninsmod vbe\ninsmod vga\nmenuentry "LiwusOS x86_64 (Instalado no HD)" {\n\tset gfxpayload=1920x1080x32,1600x900x32,1366x768x32,1024x768x32,800x600x32,auto\n\tsearch --no-floppy --file --set=root /boot/kernel.bin\n\tmultiboot2 /boot/kernel.bin mode=hd\n}\n' > build/hd_isodir/boot/grub/grub.cfg
	$(GRUB) -d /usr/lib/grub/i386-pc --fonts="" --locales="" --themes="" -o build/hd_boot.img build/hd_isodir
	tar -cf initrd.tar -C repo . --format=ustar
	tar -rf initrd.tar --format=ustar --transform='s|hd_boot.img|boot/hd_boot.img|' -C build hd_boot.img
	tar -rf initrd.tar --format=ustar -C assets .
	tar -rf initrd.tar --format=ustar --transform='s|user_init.elf|liwus_init|' -C $(OBJ_DIR)/programs user_init.elf
	tar -rf initrd.tar --format=ustar --transform='s|kilo.lxe|kilo|' -C build/liwus-sdk/bin kilo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|kilo.lxe|bin/kilo|' -C build/liwus-sdk/bin kilo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|doom.lxe|doom|' -C build/liwus-sdk/bin doom.lxe
	tar -rf initrd.tar --format=ustar --transform='s|doom.lxe|doom.lxe|' -C build/liwus-sdk/bin doom.lxe
	tar -rf initrd.tar --format=ustar --transform='s|doom.lxe|bin/doom|' -C build/liwus-sdk/bin doom.lxe
	tar -rf initrd.tar --format=ustar --transform='s|doom.lxe|bin/doom.lxe|' -C build/liwus-sdk/bin doom.lxe
	tar -rf initrd.tar --format=ustar --transform='s|native-hello.lxe|bin/native-hello.lxe|' -C build/liwus-sdk/bin native-hello.lxe
	tar -rf initrd.tar --format=ustar --transform='s|liwsh.lxe|liwsh.lxe|' -C build/liwus-sdk/bin liwsh.lxe
	tar -rf initrd.tar --format=ustar --transform='s|liwsh.lxe|bin/liwsh.lxe|' -C build/liwus-sdk/bin liwsh.lxe
	tar -rf initrd.tar --format=ustar --transform='s|ls.lxe|bin/ls.lxe|' -C build/liwus-sdk/bin ls.lxe
	tar -rf initrd.tar --format=ustar --transform='s|cat.lxe|bin/cat.lxe|' -C build/liwus-sdk/bin cat.lxe
	tar -rf initrd.tar --format=ustar --transform='s|mkdir.lxe|bin/mkdir.lxe|' -C build/liwus-sdk/bin mkdir.lxe
	tar -rf initrd.tar --format=ustar --transform='s|rm.lxe|bin/rm.lxe|' -C build/liwus-sdk/bin rm.lxe
	tar -rf initrd.tar --format=ustar --transform='s|mv.lxe|bin/mv.lxe|' -C build/liwus-sdk/bin mv.lxe
	tar -rf initrd.tar --format=ustar --transform='s|cp.lxe|bin/cp.lxe|' -C build/liwus-sdk/bin cp.lxe
	tar -rf initrd.tar --format=ustar --transform='s|touch.lxe|bin/touch.lxe|' -C build/liwus-sdk/bin touch.lxe
	tar -rf initrd.tar --format=ustar --transform='s|echo.lxe|bin/echo.lxe|' -C build/liwus-sdk/bin echo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|rmdir.lxe|bin/rmdir.lxe|' -C build/liwus-sdk/bin rmdir.lxe
	tar -rf initrd.tar --format=ustar --transform='s|stat.lxe|bin/stat.lxe|' -C build/liwus-sdk/bin stat.lxe
	tar -rf initrd.tar --format=ustar --transform='s|head.lxe|bin/head.lxe|' -C build/liwus-sdk/bin head.lxe
	tar -rf initrd.tar --format=ustar --transform='s|webtext.lxe|bin/webtext.lxe|' -C build/liwus-sdk/bin webtext.lxe
	tar -rf initrd.tar --format=ustar --transform='s|qjs.lxe|bin/qjs.lxe|' -C build/liwus-sdk/bin qjs.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|c_demo|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|c_demo.lxe|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|bin/c_demo|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|bin/c_demo.lxe|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|cdemo|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|bin/cdemo|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|demo|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|bin/demo|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|libc|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|c_demo.lxe|bin/libc|' -C build/liwus-sdk/bin c_demo.lxe
	tar -rf initrd.tar --format=ustar --transform='s|tcc.lxe|tcc|' -C build/liwus-sdk/bin tcc.lxe
	tar -rf initrd.tar --format=ustar --transform='s|tcc.lxe|tcc.lxe|' -C build/liwus-sdk/bin tcc.lxe
	tar -rf initrd.tar --format=ustar --transform='s|tcc.lxe|bin/tcc|' -C build/liwus-sdk/bin tcc.lxe
	tar -rf initrd.tar --format=ustar --transform='s|tcc.lxe|bin/tcc.lxe|' -C build/liwus-sdk/bin tcc.lxe
	tar -rf initrd.tar --format=ustar --transform='s|tcc.lxe|sys/bin/tcc|' -C build/liwus-sdk/bin tcc.lxe
	tar -rf initrd.tar --format=ustar --transform='s|tcc.lxe|sys/bin/tcc.lxe|' -C build/liwus-sdk/bin tcc.lxe
	tar -rf initrd.tar --format=ustar --transform='s|^build/liwus-sdk/include|sys/include|' build/liwus-sdk/include
	tar -rf initrd.tar --format=ustar --transform='s|^build/liwus-sdk/include|include|' build/liwus-sdk/include
	tar -rf initrd.tar --format=ustar --transform='s|libliwus.a|sys/lib/libliwus.a|' -C build/liwus-sdk/lib libliwus.a
	tar -rf initrd.tar --format=ustar --transform='s|libliwus.a|lib/libliwus.a|' -C build/liwus-sdk/lib libliwus.a
	tar -rf initrd.tar --format=ustar --transform='s|libliwus.a|sys/lib/tcc/libliwus.a|' -C build/liwus-sdk/lib libliwus.a
	tar -rf initrd.tar --format=ustar --transform='s|libc.a|sys/lib/libc.a|' -C build/liwus-sdk/lib libc.a
	tar -rf initrd.tar --format=ustar --transform='s|libc.a|lib/libc.a|' -C build/liwus-sdk/lib libc.a
	tar -rf initrd.tar --format=ustar --transform='s|libc.a|sys/lib/tcc/libc.a|' -C build/liwus-sdk/lib libc.a
	tar -rf initrd.tar --format=ustar --transform='s|libtcc1.a|sys/lib/libtcc1.a|' -C build/liwus-sdk/lib libtcc1.a
	tar -rf initrd.tar --format=ustar --transform='s|libtcc1.a|lib/libtcc1.a|' -C build/liwus-sdk/lib libtcc1.a
	tar -rf initrd.tar --format=ustar --transform='s|libtcc1.a|sys/lib/tcc/libtcc1.a|' -C build/liwus-sdk/lib libtcc1.a
	tar -rf initrd.tar --format=ustar --transform='s|crt0.o|sys/lib/crt0.o|' -C build/liwus-sdk/lib crt0.o
	tar -rf initrd.tar --format=ustar --transform='s|crt0.o|lib/crt0.o|' -C build/liwus-sdk/lib crt0.o
	tar -rf initrd.tar --format=ustar --transform='s|crt0.o|sys/lib/tcc/crt0.o|' -C build/liwus-sdk/lib crt0.o
	tar -rf initrd.tar --format=ustar --transform='s|crt1.o|sys/lib/crt1.o|' -C build/liwus-sdk/lib crt1.o
	tar -rf initrd.tar --format=ustar --transform='s|crt1.o|lib/crt1.o|' -C build/liwus-sdk/lib crt1.o
	tar -rf initrd.tar --format=ustar --transform='s|crt1.o|sys/lib/tcc/crt1.o|' -C build/liwus-sdk/lib crt1.o
	tar -rf initrd.tar --format=ustar --transform='s|crti.o|sys/lib/crti.o|' -C build/liwus-sdk/lib crti.o
	tar -rf initrd.tar --format=ustar --transform='s|crti.o|lib/crti.o|' -C build/liwus-sdk/lib crti.o
	tar -rf initrd.tar --format=ustar --transform='s|crti.o|sys/lib/tcc/crti.o|' -C build/liwus-sdk/lib crti.o
	tar -rf initrd.tar --format=ustar --transform='s|crtn.o|sys/lib/crtn.o|' -C build/liwus-sdk/lib crtn.o
	tar -rf initrd.tar --format=ustar --transform='s|crtn.o|lib/crtn.o|' -C build/liwus-sdk/lib crtn.o
	tar -rf initrd.tar --format=ustar --transform='s|crtn.o|sys/lib/tcc/crtn.o|' -C build/liwus-sdk/lib crtn.o
	tar -rf initrd.tar --format=ustar --transform='s|elf64.ld|sys/lib/elf64.ld|' -C build/liwus-sdk/share/liwus elf64.ld
	tar -rf initrd.tar --format=ustar --transform='s|elf64.ld|share/liwus/elf64.ld|' -C build/liwus-sdk/share/liwus elf64.ld
	tar -rf initrd.tar --format=ustar --transform='s|^programs/tcc/include|sys/lib/tcc/include|' programs/tcc/include
	tar -rf initrd.tar --format=ustar --transform='s|^programs/tcc/include|include|' programs/tcc/include
	tar -rf initrd.tar --format=ustar --transform='s|^programs/tcc/include|sys/include|' programs/tcc/include
	tar -rf initrd.tar --format=ustar --owner=1000 --group=1000 --transform='s|examples/hello.c|hello.c|' examples/hello.c
	tar -rf initrd.tar --format=ustar --owner=1000 --group=1000 --transform='s|examples/hello.c|sys/src/hello.c|' examples/hello.c
	tar -rf initrd.tar --format=ustar --transform='s|c_example.elf|c_example|' -C $(TEST_OBJ_DIR)/ring3 c_example.elf
	tar -rf initrd.tar --format=ustar --transform='s|c_example.elf|bin/c_example|' -C $(TEST_OBJ_DIR)/ring3 c_example.elf
	cp initrd.tar isodir/boot/initrd.tar
	$(GRUB) -o $@ isodir

.PHONY: update-feed
update-feed: $(ISO_IMAGE)
	python3 scripts/package_system_update.py --boot-image build/hd_boot.img --initrd initrd.tar \
		--output $(UPDATE_FEED_DIR) --version $(UPDATE_VERSION) --date $(UPDATE_DATE) \
		--base-url $(UPDATE_BASE_URL) --notes "$(UPDATE_NOTES)"

# ------------------------------------------------------------
# Testes (kernel-side)
# ------------------------------------------------------------

test-harness:
	python3 tests/harness/validate_serial.py --self-test

test-unit: test-harness $(TEST_OBJ_DIR)/host/test_vma_host $(TEST_OBJ_DIR)/host/test_liwsh_parser $(TEST_OBJ_DIR)/host/test_usb_hid $(TEST_OBJ_DIR)/host/test_usb_xhci_ring $(TEST_OBJ_DIR)/host/test_usb_ehci_schedule $(TEST_OBJ_DIR)/host/test_webdom $(TEST_OBJ_DIR)/host/test_html_css_nth $(TEST_OBJ_DIR)/host/test_html_sibling_map $(TEST_OBJ_DIR)/host/test_html_external_script $(TEST_OBJ_DIR)/host/test_quickjs_module $(TEST_OBJ_DIR)/host/test_qjs_module_url $(TEST_OBJ_DIR)/host/test_browser_url $(TEST_OBJ_DIR)/host/test_liwus_malloc
	$(TEST_OBJ_DIR)/host/test_vma_host
	$(TEST_OBJ_DIR)/host/test_liwsh_parser
	$(TEST_OBJ_DIR)/host/test_usb_hid
	$(TEST_OBJ_DIR)/host/test_usb_xhci_ring
	$(TEST_OBJ_DIR)/host/test_usb_ehci_schedule
	$(TEST_OBJ_DIR)/host/test_webdom
	$(TEST_OBJ_DIR)/host/test_html_css_nth
	$(TEST_OBJ_DIR)/host/test_html_sibling_map
	$(TEST_OBJ_DIR)/host/test_html_external_script
	$(TEST_OBJ_DIR)/host/test_quickjs_module
	$(TEST_OBJ_DIR)/host/test_qjs_module_url
	$(TEST_OBJ_DIR)/host/test_browser_url
	$(TEST_OBJ_DIR)/host/test_liwus_malloc

$(TEST_OBJ_DIR)/host/test_browser_url: tests/host/test_browser_url.c \
		src/kernel/gui/browser_url.c include/gui/browser_url.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iinclude -o $@ tests/host/test_browser_url.c src/kernel/gui/browser_url.c

$(TEST_OBJ_DIR)/host/liwus_malloc_test_impl.o: lib/liwus/malloc.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections \
		-Dmalloc=liw_malloc -Dcalloc=liw_calloc -Drealloc=liw_realloc -Dfree=liw_free \
		-Dexit=liw_libc_exit -Dabort=liw_libc_abort -Dabs=liw_libc_abs \
		-Iinclude -c -o $@ $<

$(TEST_OBJ_DIR)/host/test_liwus_malloc: tests/host/test_liwus_malloc.c \
		$(TEST_OBJ_DIR)/host/liwus_malloc_test_impl.o
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-Iinclude -o $@ tests/host/test_liwus_malloc.c \
		$(TEST_OBJ_DIR)/host/liwus_malloc_test_impl.o

$(TEST_OBJ_DIR)/host/test_html_css_nth: tests/host/test_html_css_nth.c \
		src/kernel/gui/html_css.c include/gui/html_css.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iinclude -Iinclude/kernel -Iinclude/gui -Isrc/kernel/gui/lvgl \
		-DLV_CONF_INCLUDE_SIMPLE -DHTML_CSS_HOST_TEST -o $@ tests/host/test_html_css_nth.c \
		src/kernel/gui/html_css.c

$(TEST_OBJ_DIR)/host/test_html_sibling_map: tests/host/test_html_sibling_map.c \
		src/kernel/gui/html_view.c src/kernel/gui/html_css.c include/gui/html_css.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-Iinclude -Iinclude/kernel -Iinclude/drivers -Iinclude/fs -Iinclude/uapi \
		-Iinclude/net -Iinclude/gui -Isrc/kernel/gui -Iinclude/libc \
		-Isrc/kernel/gui/lvgl -DLV_CONF_INCLUDE_SIMPLE -Isrc/net \
		-Isrc/net/lwip/include -Isrc/net/bearssl/inc -Isrc/kernel \
		-Isrc/kernel/terminal -Itests -o $@ tests/host/test_html_sibling_map.c \
		src/kernel/gui/html_css.c

$(TEST_OBJ_DIR)/host/test_html_external_script: tests/host/test_html_external_script.c \
		src/kernel/gui/html_view.c include/gui/html_view.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-Iinclude -Iinclude/kernel -Iinclude/drivers -Iinclude/fs -Iinclude/uapi \
		-Iinclude/net -Iinclude/gui -Isrc/kernel/gui -Iinclude/libc \
		-Isrc/kernel/gui/lvgl -DLV_CONF_INCLUDE_SIMPLE -Isrc/net \
		-Isrc/net/lwip/include -Isrc/net/bearssl/inc -Isrc/kernel \
		-Isrc/kernel/terminal -Itests -o $@ tests/host/test_html_external_script.c \
		src/kernel/gui/html_view.c

$(TEST_OBJ_DIR)/host/test_quickjs_module: tests/host/test_quickjs_module.c \
		programs/qjs/module_url.c programs/qjs/module_url.h \
		third_party/quickjs/quickjs.c third_party/quickjs/libregexp.c \
		third_party/quickjs/libunicode.c third_party/quickjs/dtoa.c \
		third_party/quickjs/cutils.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough \
		-Wno-misleading-indentation -Wno-return-type \
		-DCONFIG_LIWUS -DCONFIG_VERSION='"2026-06-04-liwus"' \
		-Ithird_party/quickjs -o $@ tests/host/test_quickjs_module.c \
		third_party/quickjs/quickjs.c third_party/quickjs/libregexp.c \
		third_party/quickjs/libunicode.c third_party/quickjs/dtoa.c \
		third_party/quickjs/cutils.c programs/qjs/module_url.c -lm -ldl

$(TEST_OBJ_DIR)/host/test_qjs_module_url: tests/host/test_qjs_module_url.c \
		programs/qjs/module_url.c programs/qjs/module_url.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o $@ tests/host/test_qjs_module_url.c programs/qjs/module_url.c

$(TEST_OBJ_DIR)/host/test_webdom: tests/host/test_webdom.c lib/liwus/webdom.c \
		lib/liwus/webdom_entities.inc include/liwus/webdom.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iinclude -o $@ tests/host/test_webdom.c lib/liwus/webdom.c

$(TEST_OBJ_DIR)/host/test_usb_ehci_schedule: tests/host/test_usb_ehci_schedule.c \
		include/drivers/usb_ehci_schedule.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iinclude -Iinclude/drivers -o $@ $<

$(TEST_OBJ_DIR)/host/test_usb_hid: tests/host/test_usb_hid.c \
		src/drivers/usb_hid.c include/drivers/usb_hid.h \
		include/drivers/keyboard.h include/drivers/mouse.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-DUSB_HID_HOST_TEST \
		-Iinclude -Iinclude/drivers -o $@ tests/host/test_usb_hid.c \
		src/drivers/usb_hid.c

$(TEST_OBJ_DIR)/host/test_usb_xhci_ring: tests/host/test_usb_xhci_ring.c \
		src/drivers/usb_xhci_ring.c include/drivers/usb_xhci_ring.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iinclude -Iinclude/drivers -o $@ tests/host/test_usb_xhci_ring.c \
		src/drivers/usb_xhci_ring.c

$(TEST_OBJ_DIR)/host/test_liwsh_parser: tests/host/test_liwsh_parser.c \
		programs/liwsh/parser.c programs/liwsh/parser.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iprograms/liwsh -o $@ tests/host/test_liwsh_parser.c \
		programs/liwsh/parser.c

test-stress: $(TEST_OBJ_DIR)/host/test_vma_host
	$(TEST_OBJ_DIR)/host/test_vma_host --stress

test-all: test-unit test ring3-test smap-test

$(TEST_OBJ_DIR)/host/test_vma_host: tests/host/test_vma_host.c src/kernel/mm/vma.c \
		include/kernel/vma.h include/kernel/vmm.h include/kernel/kheap.h \
		include/kernel/usercopy.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iinclude/kernel -Iinclude -o $@ tests/host/test_vma_host.c \
		src/kernel/mm/vma.c

test-build: $(TEST_ISO_IMAGE)

.PHONY: test-image-force
test-image-force:

$(TEST_ISO_IMAGE): Makefile $(TEST_KERNEL_BIN) $(BOOT_DIR)/grub.cfg $(DISK_IMAGE) $(RING3_USER_ELF) $(RING3_EXEC_ELF) $(RING3_C_APP_ELF) $(RING3_PIE_ELF) $(RING3_LINUX_SYSCALL_ELF) $(RING3_LINUX_CLONE_ELF) $(RING3_PERMISSION_ELF) $(RING3_WEBJS_ELF) $(QJS_LXE) $(LINUX_STATIC_TEST_DEP) $(NATIVE_HELLO_LXE) $(NATIVE_UTIL_LXES) $(EXTERNAL_SDK_TEST_LXE) test-image-force
	@mkdir -p repo-test
	: > repo-test/test_mode
	@mkdir -p isodir-test/boot/grub
	cp -f $(TEST_KERNEL_BIN) isodir-test/boot/kernel.bin
	cp -f $(BOOT_DIR)/grub.cfg isodir-test/boot/grub/grub.cfg
	cp -f $(RING3_USER_ELF) repo-test/ring3_suite
	cp -f $(RING3_EXEC_ELF) repo-test/exec_target
	cp -f $(RING3_C_APP_ELF) repo-test/c_example
	cp -f $(RING3_PIE_ELF) repo-test/pie_probe
	cp -f $(RING3_LINUX_SYSCALL_ELF) repo-test/linux_syscall_probe
	cp -f $(RING3_LINUX_CLONE_ELF) repo-test/linux_clone_probe
	cp -f $(RING3_PERMISSION_ELF) repo-test/permission_probe
	cp -f $(RING3_WEBJS_ELF) repo-test/qjs_pipe_probe.lxe
	cp -f $(NATIVE_HELLO_LXE) repo-test/native-hello.lxe
	mkdir -p repo-test/bin
	cp -f $(QJS_LXE) repo-test/bin/qjs.lxe
	cp -f $(NATIVE_HELLO_LXE) repo-test/bin/native-hello.lxe
	cp -f $(NATIVE_UTIL_LXES) repo-test/bin/
	cp -f $(EXTERNAL_SDK_TEST_LXE) repo-test/external-test.lxe
	rm -f repo-test/linux_glibc_hello
	if test -n "$(LINUX_STATIC_ELF)"; then cp -f "$(LINUX_STATIC_ELF)" repo-test/linux_glibc_hello; fi
	cp -f tests/ring3/invalid_elf repo-test/invalid_elf
	chmod 0755 repo-test/invalid_elf
	tar -cf initrd-test.tar -C repo-test . --format=ustar
	rm -f repo-test/ring3_suite repo-test/qjs_pipe_probe.lxe
	rm -f repo-test/exec_target repo-test/c_example repo-test/pie_probe repo-test/linux_syscall_probe repo-test/linux_clone_probe repo-test/permission_probe repo-test/native_hello repo-test/native-hello.lxe repo-test/bin/native-hello.lxe repo-test/invalid_elf
	cp -f initrd-test.tar isodir-test/boot/initrd.tar
	$(GRUB) -o $@ isodir-test

test: test-unit $(TEST_ISO_IMAGE)
	@mkdir -p test-logs
	@serial_log=$$(mktemp test-logs/test-XXXXXX.serial); \
	timeout 90 $(QEMU) -machine q35 -m 2048 -display none -monitor none \
		-serial file:$$serial_log -no-reboot \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-drive file=$(DISK_IMAGE),format=raw,if=none,id=disk \
		-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
		-cdrom $(TEST_ISO_IMAGE); qemu_rc=$$?; \
	python3 tests/harness/validate_serial.py --mode test \
		--qemu-exit $$qemu_rc --log $$serial_log \
		--linux-output "$(LINUX_STATIC_EXPECT)"; check_rc=$$?; \
	cp -f $$serial_log test_serial.log; \
	exit $$check_rc

# ------------------------------------------------------------
# Execucao
# ------------------------------------------------------------

QEMU_COMMON = -m 2048 -no-reboot \
              -drive file=$(DISK_IMAGE),format=raw,if=none,id=disk \
              -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
              -netdev user,id=net0 -device e1000,netdev=net0
QEMU_BASE = -machine q35 $(QEMU_COMMON)
QEMU_USB_BASE = -machine pc-q35-11.1,i8042=off $(QEMU_COMMON) \
	-device ich9-usb-ehci1,id=ehci1,addr=1a.0 \
	-device ich9-usb-ehci2,id=ehci2,addr=1d.0

.PHONY: usb-probe-test usb-ehci-start-test
run: all
	$(QEMU) $(QEMU_USB_BASE) \
		-device usb-kbd,bus=ehci1.0,port=1 \
		-device usb-mouse,bus=ehci1.0,port=2 \
		-cdrom $(ISO_IMAGE) -display curses -monitor none \
		-serial file:qemu_serial.log

usb-probe-test: all
	python3 tests/harness/run_usb_input_test.py --qemu "$(QEMU)" \
		--disk "$(DISK_IMAGE)" --iso "$(ISO_IMAGE)"

usb-ehci-start-test: all
	python3 tests/harness/run_usb_ehci_start_test.py --qemu "$(QEMU)" \
		--disk "$(DISK_IMAGE)" --iso "$(ISO_IMAGE)"

run-log: all
	rm -f qemu_serial.log
	$(QEMU) $(QEMU_BASE) -cdrom $(ISO_IMAGE) -display none -monitor none \
		-serial file:qemu_serial.log -d guest_errors -D qemu_debug.log

clean:
	rm -rf $(OBJ_DIR) $(TEST_OBJ_DIR) isodir isodir-test repo repo-test
	rm -f $(KERNEL_BIN) $(TEST_KERNEL_BIN) $(ISO_IMAGE) $(TEST_ISO_IMAGE) \
	      initrd.tar initrd-test.tar test_serial.log qemu_serial.log qemu_debug.log
	@echo "liwus_disk.img preservado (remova manualmente para disco limpo)"
