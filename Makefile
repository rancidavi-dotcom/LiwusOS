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
DISK_IMAGE = liwus_disk.img

BOOT_SRCS = $(BOOT_DIR)/boot.s $(BOOT_DIR)/interrupt.s

KERNEL_C_SRCS = $(wildcard src/kernel/arch/x86_64/*.c) \
                $(wildcard src/kernel/core/*.c) \
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
            tests/test_runner_ring3.c

TEST_OBJS = $(patsubst %.c,$(TEST_OBJ_DIR)/%.o,$(TEST_SRCS))
# Kernel compilado com -DKERNEL_TEST (substitui o kernel.o normal)
TEST_KERNEL_OBJS = $(filter-out $(OBJ_DIR)/src/kernel/core/kernel.o,$(KERNEL_OBJS)) \
                   $(TEST_OBJ_DIR)/src/kernel/core/kernel.o \
                   $(TEST_OBJS)

TEST_CFLAGS = $(CFLAGS) -DKERNEL_TEST
RING3_USER_ELF = $(TEST_OBJ_DIR)/ring3/suite.elf
RING3_EXEC_ELF = $(TEST_OBJ_DIR)/ring3/exec_target.elf
RING3_C_APP_ELF = $(TEST_OBJ_DIR)/ring3/c_example.elf
RING3_USER_CFLAGS = -std=gnu99 -O2 -ffreestanding -fno-pie -fno-pic \
	-fno-stack-protector -fno-asynchronous-unwind-tables -m64 -mno-red-zone \
	-Iinclude/uapi
USER_SHELL_ELF = $(OBJ_DIR)/programs/user_shell.elf
USER_SHELL_CFLAGS = $(RING3_USER_CFLAGS) -Iprograms/c_example
USER_INIT_ELF = $(OBJ_DIR)/programs/user_init.elf
USER_EDITOR_ELF = $(OBJ_DIR)/programs/user_editor.elf

.PHONY: all run run-log test test-build ring3-test user-c-example clean

user-c-example: $(RING3_C_APP_ELF)

ring3-test: $(TEST_ISO_IMAGE)
	timeout 90 $(QEMU) -machine q35 -m 2048 -display none -monitor none \
		-serial file:/tmp/liwusos-ring3-integration.serial -no-reboot \
		-cdrom $(TEST_ISO_IMAGE) || test $$? -eq 124
	@grep -E 'RING3 (PASS|FAIL|RESULT)|RESULT:|KERNEL PANIC|LIWUS_BOOT_READY' \
		/tmp/liwusos-ring3-integration.serial || true
	@grep -q '\[RING3 RESULT\] ALL PASS' /tmp/liwusos-ring3-integration.serial
	@grep -q 'RESULT: ALL PASS' /tmp/liwusos-ring3-integration.serial

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

$(TEST_OBJ_DIR)/tests/%.o: tests/%.c tests/framework.h
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(TEST_CFLAGS)

$(TEST_OBJ_DIR)/ring3/suite.o: tests/ring3/suite.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(RING3_USER_CFLAGS)

$(RING3_USER_ELF): $(TEST_OBJ_DIR)/ring3/suite.o tests/ring3/suite.ld
	$(LD) -nostdlib -static -no-pie -Wl,--build-id=none \
		-Wl,-T,tests/ring3/suite.ld -o $@ $(TEST_OBJ_DIR)/ring3/suite.o

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

$(ISO_IMAGE): Makefile isodir/boot/kernel.bin isodir/boot/grub/grub.cfg $(DISK_IMAGE) $(USER_SHELL_ELF) $(USER_INIT_ELF) $(USER_EDITOR_ELF) $(RING3_C_APP_ELF)
	@mkdir -p repo
	tar -cf initrd.tar -C repo . --format=ustar
	tar -rf initrd.tar --format=ustar --transform='s|user_shell.elf|liwus_shell|' -C $(OBJ_DIR)/programs user_shell.elf
	tar -rf initrd.tar --format=ustar --transform='s|user_shell.elf|bin/liwus_shell|' -C $(OBJ_DIR)/programs user_shell.elf
	tar -rf initrd.tar --format=ustar --transform='s|user_init.elf|liwus_init|' -C $(OBJ_DIR)/programs user_init.elf
	tar -rf initrd.tar --format=ustar --transform='s|user_editor.elf|edit|' -C $(OBJ_DIR)/programs user_editor.elf
	tar -rf initrd.tar --format=ustar --transform='s|user_editor.elf|bin/edit|' -C $(OBJ_DIR)/programs user_editor.elf
	tar -rf initrd.tar --format=ustar --transform='s|c_example.elf|c_example|' -C $(TEST_OBJ_DIR)/ring3 c_example.elf
	tar -rf initrd.tar --format=ustar --transform='s|c_example.elf|bin/c_example|' -C $(TEST_OBJ_DIR)/ring3 c_example.elf
	cp initrd.tar isodir/boot/initrd.tar
	$(GRUB) -o $@ isodir

# ------------------------------------------------------------
# Testes (kernel-side)
# ------------------------------------------------------------

test-build: $(TEST_ISO_IMAGE)

$(TEST_ISO_IMAGE): Makefile $(TEST_KERNEL_BIN) $(BOOT_DIR)/grub.cfg $(DISK_IMAGE) $(RING3_USER_ELF) $(RING3_EXEC_ELF) $(RING3_C_APP_ELF)
	@mkdir -p repo-test
	: > repo-test/test_mode
	@mkdir -p isodir-test/boot/grub
	cp -f $(TEST_KERNEL_BIN) isodir-test/boot/kernel.bin
	cp -f $(BOOT_DIR)/grub.cfg isodir-test/boot/grub/grub.cfg
	cp -f $(RING3_USER_ELF) repo-test/ring3_suite
	cp -f $(RING3_EXEC_ELF) repo-test/exec_target
	cp -f $(RING3_C_APP_ELF) repo-test/c_example
	cp -f tests/ring3/invalid_elf repo-test/invalid_elf
	tar -cf initrd-test.tar -C repo-test . --format=ustar
	rm -f repo-test/ring3_suite
	rm -f repo-test/exec_target repo-test/c_example repo-test/invalid_elf
	cp -f initrd-test.tar isodir-test/boot/initrd.tar
	$(GRUB) -o $@ isodir-test

test: $(TEST_ISO_IMAGE)
	rm -f test_serial.log
	timeout 90 $(QEMU) -machine q35 -m 2048 -display none -monitor none \
		-serial file:test_serial.log -no-reboot \
		-drive file=$(DISK_IMAGE),format=raw,if=none,id=disk \
		-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
		-cdrom $(TEST_ISO_IMAGE) || true
	@echo "---- serial ----"
	@grep -E '=== TEST RUNNER|RESULT:|FAIL|KERNEL PANIC|LIWUS_BOOT_READY' test_serial.log || true
	@if grep -q 'RESULT: ALL PASS' test_serial.log; then echo "TESTES OK"; \
	 else echo "TESTES FALHARAM"; exit 1; fi

# ------------------------------------------------------------
# Execucao
# ------------------------------------------------------------

QEMU_BASE = -machine q35 -m 2048 -no-reboot \
            -drive file=$(DISK_IMAGE),format=raw,if=none,id=disk \
            -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0

run: all
	$(QEMU) $(QEMU_BASE) -cdrom $(ISO_IMAGE) -serial stdio

run-log: all
	rm -f qemu_serial.log
	$(QEMU) $(QEMU_BASE) -cdrom $(ISO_IMAGE) -display none -monitor none \
		-serial file:qemu_serial.log -d guest_errors -D qemu_debug.log

clean:
	rm -rf $(OBJ_DIR) $(TEST_OBJ_DIR) isodir isodir-test repo repo-test
	rm -f $(KERNEL_BIN) $(TEST_KERNEL_BIN) $(ISO_IMAGE) $(TEST_ISO_IMAGE) \
	      initrd.tar initrd-test.tar test_serial.log qemu_serial.log qemu_debug.log
	@echo "liwus_disk.img preservado (remova manualmente para disco limpo)"
