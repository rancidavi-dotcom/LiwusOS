#!/bin/bash
# Headless test driver: boots LiwusOS with the virtual pendrive attached.
# Usage: ./scripts/qa_run.sh [seconds]
set -eu
cd "$(dirname "$0")/.."
SECS="${1:-30}"

rm -f /tmp/pen_run_serial.log /tmp/pen_rec.wav /tmp/liwus_qmp.sock qa_serial.log

qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -drive id=disk,file=liwus_disk.img,if=none,format=raw \
    -device ahci,id=ahci \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -blockdev "driver=raw,node-name=pen,file.driver=file,file.filename=$PWD/pendrive.img" \
    -device am53c974,id=scsi0 \
    -device scsi-hd,id=pendrive_disk,drive=pen,bus=scsi0.0 \
    -qmp unix:/tmp/liwus_qmp.sock,server=on,wait=off \
    -m 512 -display none \
    -serial file:/tmp/pen_run_serial.log \
    -monitor none -no-reboot \
    -audiodev wav,id=aud0,path=/tmp/pen_rec.wav \
    -device AC97,audiodev=aud0 \
    >/tmp/pen_qemu.log 2>&1 &

QPID=$!
echo "qemu pid=$QPID"
sleep "$SECS"
kill -9 "$QPID" 2>/dev/null || true
wait "$QPID" 2>/dev/null || true
cp -f /tmp/pen_run_serial.log qa_serial.log 2>/dev/null || true
cp -f /tmp/pen_rec.wav qa_rec.wav 2>/dev/null || true
cp -f /tmp/pen_qemu.log qa_qemu.log 2>/dev/null || true
echo "=== last serial lines ==="
tail -25 qa_serial.log