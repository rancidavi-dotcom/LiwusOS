#!/bin/bash
# Repacks pendrive.img (FAT32 superfloppy) from whatever is dropped in pen/
# Usage: ./scripts/pack_pen.sh
set -eu
cd "$(dirname "$0")/.."

IMG="pendrive.img"
rm -f "$IMG"
mformat -F -C -i "$IMG" -T 65536 -c 1 -h 64 -s 32 ::
n=0
shopt -s nullglob
for f in pen/*.mp3 pen/*.MP3; do
    [ -e "$f" ] || continue
    mcopy -o -i "$IMG" "$f" ::
    n=$((n + 1))
    echo "packed: $f"
done
echo "pendrive.img ready (FAT32, $n mp3)"