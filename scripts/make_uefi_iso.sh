#!/bin/bash
# ============================================================
# LiwusOS - Build do ISO com boot UEFI
#
# Requer o diretorio 'isodir' ja preparado (feito por 'make liwusos.iso').
#
# Estrategia:
#   1) Se os modulos x86_64-efi estiverem em /usr/lib/grub (pacote
#      grub-efi-amd64-bin), grub-mkrescue ja gera ISO HIBRIDO (BIOS+UEFI).
#   2) Caso contrario, extrai grub-efi-amd64-bin para build/grub-efi (sem
#      root) e usa 'unshare -rm' + bind mount sobre /usr/lib/grub para
#      tambem gerar um ISO HIBRIDO.
#   3) Se 'unshare' nao estiver disponivel, gera um ISO somente-UEFI.
#
# Uso: bash scripts/make_uefi_iso.sh [saida.iso]
# ============================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

OUT="${1:-liwusos-uefi.iso}"

if [ ! -d isodir ]; then
    echo "[uefi] isodir ausente; rode 'make liwusos.iso' primeiro" >&2
    exit 1
fi

MERGE_DIR=""

cleanup() {
    [ -n "$MERGE_DIR" ] && rm -rf "$MERGE_DIR"
    return 0
}
trap cleanup EXIT

if [ -d /usr/lib/grub/x86_64-efi ]; then
    echo "[uefi] modulos x86_64-efi do sistema encontrados -> ISO hibrido (BIOS+UEFI)"
    grub-mkrescue --disable-shim-lock -o "$OUT" isodir
    echo "[uefi] ISO pronto: $OUT"
    exit 0
fi

# --- Modulos EFI ausentes: obter uma copia local (sem root) ---
EFI_DIR="$ROOT_DIR/build/grub-efi/usr/lib/grub/x86_64-efi"
if [ ! -f "$EFI_DIR/modinfo.sh" ]; then
    echo "[uefi] grub-efi-amd64-bin ausente; baixando para build/grub-efi ..."
    TMP="$ROOT_DIR/build/grub-efi-tmp"
    rm -rf "$TMP"; mkdir -p "$TMP"
    ( cd "$TMP" && apt-get download grub-efi-amd64-bin >/dev/null )
    DEB="$(ls "$TMP"/grub-efi-amd64-bin_*.deb | head -1)"
    rm -rf "$ROOT_DIR/build/grub-efi"
    mkdir -p "$ROOT_DIR/build/grub-efi"
    dpkg-deb -x "$DEB" "$ROOT_DIR/build/grub-efi"
    rm -rf "$TMP"
fi

# --- Tentar ISO hibrido via user-namespace (bind mount de /usr/lib/grub) ---
if command -v unshare >/dev/null 2>&1 && unshare -rm true 2>/dev/null; then
    MERGE_DIR="$(mktemp -d)"
    cp -r /usr/lib/grub/i386-pc "$MERGE_DIR/"
    cp -r "$EFI_DIR" "$MERGE_DIR/x86_64-efi"
    echo "[uefi] gerando ISO HIBRIDO via unshare+bind mount"
    if unshare -rm bash -c "mount --bind '$MERGE_DIR' /usr/lib/grub && grub-mkrescue --disable-shim-lock -o '$OUT' isodir"; then
        echo "[uefi] ISO pronto (hibrido): $OUT"
        exit 0
    fi
    echo "[uefi] unshare/bind falhou; caindo para ISO somente-UEFI" >&2
fi

echo "[uefi] gerando ISO somente-UEFI usando $EFI_DIR"
grub-mkrescue -d "$EFI_DIR" --disable-shim-lock -o "$OUT" isodir
echo "[uefi] ISO pronto (somente-UEFI): $OUT"
