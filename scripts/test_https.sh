#!/bin/bash
cd /mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS
timeout 120 qemu-system-x86_64 -cdrom liwusos.iso \
  -device rtl8139,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:2222,hostfwd=tcp::8080-:80 \
  -m 512 -display none -monitor none -no-reboot \
  -serial file:/tmp/https_test2.log -accel tcg 2>&1 | tail -5