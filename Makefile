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

KERNEL_INCLUDES = -Iinclude -Iinclude/kernel -Iinclude/drivers -Iinclude/fs \
                  -Iinclude/uapi -Isrc/kernel -Isrc/kernel/terminal -Itests

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Werror $(KERNEL_INCLUDES) $(M64) \
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
LINUX_STATIC_TEST_DEP =
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
                $(wildcard src/drivers/*.c) \
                $(wildcard src/fs/*.c)

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
NATIVE_UTIL_LXES = $(MKDIR_LXE) $(RM_LXE) $(MV_LXE) $(CP_LXE) \
	$(TOUCH_LXE) $(ECHO_LXE) $(RMDIR_LXE) $(STAT_LXE) $(HEAD_LXE)
EXTERNAL_SDK_TEST_LXE = build/external-test.lxe
SDK_LIBRARY = build/liwus-sdk/lib/libliwus.a
SDK_CRT0 = build/liwus-sdk/lib/crt0.o
SDK_LINKER_SCRIPT = build/liwus-sdk/share/liwus/elf64.ld
SDK_HEADERS_STAMP = build/liwus-sdk/include/.headers-installed

.PHONY: all run run-log test test-build ring3-test smap-test user-c-example sdk sdk-install liwsh external-sdk-build shell-test app-install sdfs-put clean
.PHONY: test-harness test-unit test-stress test-all app-install-test
.NOTPARALLEL: test-all sdk shell-test app-install-test

user-c-example: $(RING3_C_APP_ELF)

sdk: $(NATIVE_HELLO_LXE) $(LIWSH_LXE) $(LS_LXE) $(CAT_LXE) \
	$(NATIVE_UTIL_LXES) $(SDK_LIBRARY) $(SDK_CRT0) \
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

lib/liwus/libliwus.a: lib/liwus/Makefile lib/liwus/syscall.c \
		lib/liwus/crt.c lib/liwus/string.c \
		$(wildcard include/liwus/*.h) include/uapi/syscall_nums.h
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

$(SDK_CRT0): lib/liwus/crt0.o
	@mkdir -p $(dir $@)
	cp -f $< $@

$(SDK_LINKER_SCRIPT): sdk/elf64.ld
	@mkdir -p $(dir $@)
	cp -f $< $@

$(SDK_HEADERS_STAMP): $(wildcard include/liwus/*.h)
	@mkdir -p $(dir $@)
	cp -R include/liwus build/liwus-sdk/include/
	touch $@

ring3-test: test-unit $(TEST_ISO_IMAGE)
	@mkdir -p test-logs
	@serial_log=$$(mktemp test-logs/ring3-XXXXXX.serial); \
	timeout 90 $(QEMU) -machine q35 -m 2048 -display none -monitor none \
		-serial file:$$serial_log -no-reboot \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-cdrom $(TEST_ISO_IMAGE); qemu_rc=$$?; \
	python3 tests/harness/validate_serial.py --mode ring3 \
		--qemu-exit $$qemu_rc --log $$serial_log; check_rc=$$?; \
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
		--qemu-exit $$qemu_rc --log $$serial_log; check_rc=$$?; \
	cp -f $$serial_log /tmp/liwusos-smap-integration.serial; \
	exit $$check_rc

all: $(KERNEL_BIN) $(ISO_IMAGE)

# ------------------------------------------------------------
# Kernel
# ------------------------------------------------------------

$(KERNEL_BIN): $(KERNEL_OBJS) $(BOOT_DIR)/linker.ld
	$(LD) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) $(LDFLAGS) \
		-nostdlib -static -Wl,--build-id=none $(KERNEL_OBJS) $(LIBGCC)

$(TEST_KERNEL_BIN): $(TEST_KERNEL_OBJS) $(BOOT_DIR)/linker.ld
	$(LD) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) $(LDFLAGS) \
		-nostdlib -static -Wl,--build-id=none $(TEST_KERNEL_OBJS) $(LIBGCC)

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

$(RING3_LINUX_SYSCALL_ELF): $(TEST_OBJ_DIR)/ring3/linux_syscall_probe.o tests/ring3/suite.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,tests/ring3/suite.ld -o $@ $<

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
	dd if=/dev/zero of=$@ bs=1M count=64 status=none

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

$(ISO_IMAGE): Makefile isodir/boot/kernel.bin isodir/boot/grub/grub.cfg $(DISK_IMAGE) $(USER_INIT_ELF) $(USER_EDITOR_ELF) $(RING3_C_APP_ELF) $(NATIVE_HELLO_LXE) $(LIWSH_LXE) $(LS_LXE) $(CAT_LXE) $(NATIVE_UTIL_LXES) assets/readme.txt
	@mkdir -p repo
	tar -cf initrd.tar -C repo . --format=ustar
	tar -rf initrd.tar --format=ustar -C assets readme.txt
	tar -rf initrd.tar --format=ustar --transform='s|user_init.elf|liwus_init|' -C $(OBJ_DIR)/programs user_init.elf
	tar -rf initrd.tar --format=ustar --transform='s|user_editor.elf|edit|' -C $(OBJ_DIR)/programs user_editor.elf
	tar -rf initrd.tar --format=ustar --transform='s|user_editor.elf|bin/edit|' -C $(OBJ_DIR)/programs user_editor.elf
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
	tar -rf initrd.tar --format=ustar --transform='s|c_example.elf|c_example|' -C $(TEST_OBJ_DIR)/ring3 c_example.elf
	tar -rf initrd.tar --format=ustar --transform='s|c_example.elf|bin/c_example|' -C $(TEST_OBJ_DIR)/ring3 c_example.elf
	cp initrd.tar isodir/boot/initrd.tar
	$(GRUB) -o $@ isodir

# ------------------------------------------------------------
# Testes (kernel-side)
# ------------------------------------------------------------

test-harness:
	python3 tests/harness/validate_serial.py --self-test

test-unit: test-harness $(TEST_OBJ_DIR)/host/test_vma_host $(TEST_OBJ_DIR)/host/test_liwsh_parser $(TEST_OBJ_DIR)/host/test_usb_hid $(TEST_OBJ_DIR)/host/test_usb_xhci_ring
	$(TEST_OBJ_DIR)/host/test_vma_host
	$(TEST_OBJ_DIR)/host/test_liwsh_parser
	$(TEST_OBJ_DIR)/host/test_usb_hid
	$(TEST_OBJ_DIR)/host/test_usb_xhci_ring

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

$(TEST_ISO_IMAGE): Makefile $(TEST_KERNEL_BIN) $(BOOT_DIR)/grub.cfg $(DISK_IMAGE) $(RING3_USER_ELF) $(RING3_EXEC_ELF) $(RING3_C_APP_ELF) $(RING3_PIE_ELF) $(RING3_LINUX_SYSCALL_ELF) $(RING3_LINUX_CLONE_ELF) $(RING3_PERMISSION_ELF) $(LINUX_STATIC_TEST_DEP) $(NATIVE_HELLO_LXE) $(NATIVE_UTIL_LXES) $(EXTERNAL_SDK_TEST_LXE)
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
	cp -f $(NATIVE_HELLO_LXE) repo-test/native-hello.lxe
	mkdir -p repo-test/bin
	cp -f $(NATIVE_HELLO_LXE) repo-test/bin/native-hello.lxe
	cp -f $(NATIVE_UTIL_LXES) repo-test/bin/
	cp -f $(EXTERNAL_SDK_TEST_LXE) repo-test/external-test.lxe
	rm -f repo-test/linux_glibc_hello
	if test -n "$(LINUX_STATIC_ELF)"; then cp -f "$(LINUX_STATIC_ELF)" repo-test/linux_glibc_hello; fi
	cp -f tests/ring3/invalid_elf repo-test/invalid_elf
	chmod 0755 repo-test/invalid_elf
	tar -cf initrd-test.tar -C repo-test . --format=ustar
	rm -f repo-test/ring3_suite
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
		--qemu-exit $$qemu_rc --log $$serial_log; check_rc=$$?; \
	cp -f $$serial_log test_serial.log; \
	exit $$check_rc

# ------------------------------------------------------------
# Execucao
# ------------------------------------------------------------

QEMU_BASE = -machine q35 -m 2048 -no-reboot \
            -drive file=$(DISK_IMAGE),format=raw,if=none,id=disk \
            -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0

.PHONY: usb-probe-test
run: all
	$(QEMU) $(QEMU_BASE) -cdrom $(ISO_IMAGE) -serial stdio

usb-probe-test: all
	@log=$$(mktemp); \
	 timeout --signal=TERM 20s $(QEMU) $(QEMU_BASE) -snapshot \
		-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
		-device usb-mouse,bus=xhci.0 -cdrom $(ISO_IMAGE) \
		-display none -monitor none -serial file:$$log; qemu_rc=$$?; \
	 python3 tests/harness/validate_usb_probe.py --log $$log \
		--qemu-exit $$qemu_rc; result=$$?; rm -f $$log; exit $$result

run-log: all
	rm -f qemu_serial.log
	$(QEMU) $(QEMU_BASE) -cdrom $(ISO_IMAGE) -display none -monitor none \
		-serial file:qemu_serial.log -d guest_errors -D qemu_debug.log

clean:
	rm -rf $(OBJ_DIR) $(TEST_OBJ_DIR) isodir isodir-test repo repo-test
	rm -f $(KERNEL_BIN) $(TEST_KERNEL_BIN) $(ISO_IMAGE) $(TEST_ISO_IMAGE) \
	      initrd.tar initrd-test.tar test_serial.log qemu_serial.log qemu_debug.log
	@echo "liwus_disk.img preservado (remova manualmente para disco limpo)"
