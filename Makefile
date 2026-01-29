# Ferramentas
CC = gcc
AS = as
OBJCOPY = objcopy

CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra
ASFLAGS = --32
LDFLAGS = -m32 -ffreestanding -O2 -nostdlib

OBJS = boot.o kernel.o gdt.o idt.o interrupt.o isr.o keyboard.o pmm.o vmm.o kheap.o timer.o task.o video.o mouse.o font.o liw.o initrd.o gui.o calc.o book.o dock.o ata.o installer.o string.o fat32.o uninstaller.o terminal.o settings.o pci.o rtl8139.o net.o wifi.o explorer.o browser.o welcome.o netstack.o launcher.o

KERNEL_BIN = kernel.bin
ISO_IMAGE = liwusos.iso
DISK_IMG = liwus_disk.img

all: $(ISO_IMAGE) $(DISK_IMG)

$(ISO_IMAGE): $(KERNEL_BIN) grub.cfg initrd.tar
	mkdir -p isodir/boot/grub
	cp $(KERNEL_BIN) isodir/boot/$(KERNEL_BIN)
	cp grub.cfg isodir/boot/grub/grub.cfg
	cp initrd.tar isodir/boot/initrd.tar
	grub-mkrescue -o $(ISO_IMAGE) isodir

$(KERNEL_BIN): linker.ld $(OBJS)
	$(CC) -T linker.ld -o $@ $(LDFLAGS) $(OBJS)
	grub-file --is-x86-multiboot $@

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.s
	$(AS) $(ASFLAGS) $< -o $@

font.o:
	cp /usr/share/consolefonts/Uni2-Terminus16.psf.gz font.psf.gz
	gunzip -f font.psf.gz
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 font.psf font.o

initrd.tar:
	mkdir -p repo
	echo "LiwusOS Theme Package" > repo/theme.liw
	echo "LiwusOS Calc Package" > repo/calc.liw
	tar -cvf initrd.tar -C repo . --format=ustar

$(DISK_IMG):
	qemu-img create -f raw $(DISK_IMG) 100M

run: $(ISO_IMAGE) $(DISK_IMG)
	qemu-system-i386 -cdrom $(ISO_IMAGE) -drive file=$(DISK_IMG),format=raw,index=0,media=disk -m 512M -boot d -full-screen -net nic,model=rtl8139 -net user

clean:
	rm -f $(OBJS) $(KERNEL_BIN) $(ISO_IMAGE) font.psf initrd.tar font.psf.gz liwus_disk.img
	rm -rf isodir repo
