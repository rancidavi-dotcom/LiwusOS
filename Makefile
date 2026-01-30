CC = gcc
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
AS = as
ASFLAGS = --32
LDFLAGS = -m32 -ffreestanding -O2 -nostdlib

# Project Layout
SRC_DIR = src
OBJ_DIR = obj
BOOT_DIR = $(SRC_DIR)/boot
KERNEL_DIR = $(SRC_DIR)/kernel
DRIVERS_DIR = $(SRC_DIR)/drivers
FS_DIR = $(SRC_DIR)/fs
NET_DIR = $(SRC_DIR)/net
GUI_DIR = $(SRC_DIR)/gui
APPS_DIR = $(SRC_DIR)/apps

# Source files
BOOT_SRCS = $(BOOT_DIR)/boot.s $(BOOT_DIR)/interrupt.s
KERNEL_SRCS = $(wildcard $(KERNEL_DIR)/*.c) $(wildcard $(KERNEL_DIR)/*.s)
DRIVERS_SRCS = $(wildcard $(DRIVERS_DIR)/*.c)
FS_SRCS = $(wildcard $(FS_DIR)/*.c)
NET_SRCS = $(wildcard $(NET_DIR)/*.c)
GUI_SRCS = $(wildcard $(GUI_DIR)/*.c)
APPS_SRCS = $(filter-out $(APPS_DIR)/liw_app.c, $(wildcard $(APPS_DIR)/*.c))

# Object files
# Function to map src to obj
to_obj = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(patsubst $(SRC_DIR)/%.s, $(OBJ_DIR)/%.o, $(1)))

OBJS = $(call to_obj, $(BOOT_SRCS) $(KERNEL_SRCS) $(DRIVERS_SRCS) $(FS_SRCS) $(NET_SRCS) $(GUI_SRCS) $(APPS_SRCS)) \
       $(OBJ_DIR)/drivers/font.o

KERNEL_BIN = kernel.bin
ISO_IMAGE = liwusos.iso

all: $(ISO_IMAGE)

$(KERNEL_BIN): $(OBJS) $(BOOT_DIR)/linker.ld
	$(CC) -T $(BOOT_DIR)/linker.ld -o $@ $(CFLAGS) -nostdlib $(OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/drivers/font.o: $(SRC_DIR)/drivers/font.psf
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf32-i386 -B i386 $< $@

$(SRC_DIR)/drivers/font.psf:
	cp /usr/share/consolefonts/Uni2-Terminus16.psf.gz font.psf.gz
	gunzip -f font.psf.gz
	mv font.psf $(SRC_DIR)/drivers/font.psf

$(ISO_IMAGE): $(KERNEL_BIN) $(BOOT_DIR)/test.elf
	# Build Host Tool
	$(CC) -Iinclude sdk/tools/liw-builder.c -o sdk/tools/liw-builder
	./sdk/tools/liw-builder src/boot/test.liw src/boot/test.elf src/boot/test_manifest.json
	
	# Build Apps
	$(CC) -m32 -nostdlib -static src/apps/liw_app.c -o liw.elf
	$(CC) -m32 -nostdlib -fno-builtin -I sdk/include sdk/lib/crt0.s apps/hello/hello.c -o apps/hello/hello.elf

	grub-file --is-x86-multiboot $(KERNEL_BIN)
	mkdir -p repo
	echo "LiwusOS Theme Package" > repo/theme.liw
	echo "LiwusOS Calc Package" > repo/calc.liw
	cp src/boot/test.elf repo/test.elf
	cp src/boot/test.liw repo/test.liw
	
	# Install Apps
	cp liw.elf repo/liw
	cp apps/hello/hello.elf repo/hello.liwpkg
	
	tar -cvf initrd.tar -C repo . --format=ustar
	mkdir -p isodir/boot/grub
	cp $(KERNEL_BIN) isodir/boot/kernel.bin
	cp $(BOOT_DIR)/grub.cfg isodir/boot/grub/grub.cfg
	cp initrd.tar isodir/boot/initrd.tar
	grub-mkrescue -o $(ISO_IMAGE) isodir

$(BOOT_DIR)/test.elf: $(BOOT_DIR)/test.s
	$(CC) -m32 -nostdlib -static $< -o $@

run: $(ISO_IMAGE)
	qemu-img create -f raw liwus_disk.img 100M
	qemu-system-i386 -cdrom $(ISO_IMAGE) -drive file=liwus_disk.img,format=raw,index=0,media=disk -m 512M -boot d -full-screen -net nic,model=rtl8139 -net user

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(KERNEL_BIN) $(ISO_IMAGE) initrd.tar liwus_disk.img src/boot/test.elf src/boot/test.liw liw.elf
	rm -rf isodir repo
