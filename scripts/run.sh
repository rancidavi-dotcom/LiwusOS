#!/bin/bash
set -e

# =============================================================
# LiwusOS - build nativo + execucao (sem Docker)
# Hardware de teste universal: PC Bay Trail / Celeron J1800
# (o mesmo emulado por 'make run' e o alvo de hardware fisico).
# Requisitos: gcc, make, grub-mkrescue/xorriso (build do ISO),
#             qemu-system-x86_64 e qemu-img (execucao).
# =============================================================

die() { echo "ERRO: $*" >&2; exit 1; }
has() { command -v "$1" >/dev/null 2>&1; }

has gcc  || die "gcc nao encontrado. Instale: apt-get install build-essential"
has make || die "make nao encontrado. Instale: apt-get install make"

# ---- Build do kernel + ISO ----
# SKIP_BUILD=1 ./run.sh  -> usa os ISOs que ja existem
if [ "${SKIP_BUILD:-0}" = "1" ]; then
    [ -f liwusos.iso ] || die "SKIP_BUILD=1 mas liwusos.iso nao existe."
else
    has grub-mkrescue || die "grub-mkrescue nao encontrado. Instale: apt-get install grub-pc-bin grub-common"
    has xorriso       || die "xorriso nao encontrado. Instale: apt-get install xorriso"
    has grub-file     || die "grub-file nao encontrado. Instale: apt-get install grub-common"
    echo "==> Compilando kernel + ISO (make all) ..."
    make all || die "Falha no build. Veja a saida acima."
    echo "==> Gerando ISO hibrido UEFI (make uefi-iso) ..."
    make uefi-iso || die "Falha ao gerar liwusos-uefi.iso."
fi

# ---- Disco persistente (so cria se nao existir) ----
# Por padrao, o mesmo arquivo acompanha o projeto entre todos os boots.
# Defina LIWUS_DISK_IMAGE=/caminho/outro/disco.img para usar outro disco.
DISK_IMAGE="${LIWUS_DISK_IMAGE:-$PWD/liwus_disk.img}"
if [ ! -f "$DISK_IMAGE" ]; then
    has qemu-img || die "qemu-img nao encontrado. Instale: apt-get install qemu-utils"
    echo "==> Criando disco persistente $DISK_IMAGE (512MB) ..."
    qemu-img create -f raw "$DISK_IMAGE" 512M
fi
echo "==> Disco persistente: $DISK_IMAGE"

# ---- Audio ----
# O som interno (Intel HDA) so chega aos alto-falantes do host se o QEMU
# tiver um "audio backend". O backend depende da plataforma:
#   Windows (MSYS2/Git Bash):  dsound
#   WSL2 com WSLg:             pa  (PulseAudio -> alto-falantes do Windows)
#   outro Linux:               sdl (fallback)
# Para forcar outro backend:  AUDIO_BACKEND=wav ./run.sh  (grava audio.wav)
pick_backend() {
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) echo "dsound" ; return ;;
    esac
    if [ -S /mnt/wslg/PulseServer ]; then
        echo "pa"
        return
    fi
    echo "sdl"
}
if [ -z "${AUDIO_BACKEND:-}" ]; then
    AUDIO_BACKEND=$(pick_backend)
fi
if [ "$AUDIO_BACKEND" = "pa" ] && [ -S /mnt/wslg/PulseServer ]; then
    export PULSE_SERVER=/mnt/wslg/PulseServer
fi
echo "==> Audio backend: $AUDIO_BACKEND"

# ---- Pendrive virtual ----
# Pasta "pen/" = drop zone. O packer regera pendrive.img (FAT32) quando o
# conteudo muda; o watcher repluga o disco SCSI via QMP (PEN_WATCH=1).
if [ ! -f pendrive.img ]; then
    echo "==> Criando pendrive.img (FAT32) ..."
    ./scripts/pack_pen.sh
fi

if [ "${PEN_WATCH:-0}" = "1" ]; then
    echo "==> Iniciando watcher do pendrive ..."
    (python3 scripts/pen_watch.py &) || true
fi

# ---- KVM (virtualizacao acelerada) ----
# No WSL2 precisa de "Virtual Machine Platform" ativado no Windows
# e reiniciar WSL:  wsl --shutdown
# Use: KVM=1 ./run.sh
KVM_FLAG=""
CPU="max"
if [ "${KVM:-0}" = "1" ]; then
    if [ -w /dev/kvm ]; then
        KVM_FLAG="-enable-kvm"
        CPU="host"
        echo "==> KVM ativado (aceleracao de hardware)"
    else
        echo "AVISO: KVM=1 mas /dev/kvm nao acessivel. Ative 'Virtual Machine Platform' no Windows e reinicie o WSL."
    fi
fi

# ---- Execucao ----
# Hardware universal (J1800): q35, 2 GB, 2 vCPU, AHCI, xHCI+EHCI, Intel HDA,
# RTL8139 - identico ao 'make run'. Firmware UEFI (OVMF) quando disponivel;
# senao cai para SeaBIOS. Adiciona os extras deste script: serial, pendrive
# SCSI e QMP. Forca display via X11 (nao Wayland) para evitar crash no WSL.
OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS="${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
BOOT_ARGS=()
if [ -f liwusos-uefi.iso ] && [ -f "$OVMF_CODE" ]; then
    mkdir -p build
    [ -f build/uefi_vars.fd ] || cp "$OVMF_VARS" build/uefi_vars.fd
    BOOT_ARGS=(
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE"
        -drive if=pflash,format=raw,file=build/uefi_vars.fd
        -cdrom liwusos-uefi.iso
    )
    echo "==> Firmware: UEFI (OVMF)"
else
    BOOT_ARGS=(-cdrom liwusos.iso)
    echo "==> Firmware: SeaBIOS (OVMF/liwusos-uefi.iso ausente)"
fi

exec env GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 qemu-system-x86_64 $KVM_FLAG \
    -machine q35 -smp 2 -cpu "$CPU" -m 2048 \
    "${BOOT_ARGS[@]}" \
    -drive id=disk,file="$DISK_IMAGE",if=none,format=raw \
    -device ahci,id=ahci \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -blockdev "driver=raw,node-name=pen,file.driver=file,file.filename=$PWD/pendrive.img" \
    -device am53c974,id=scsi0 \
    -device scsi-hd,id=pendrive_disk,drive=pen,bus=scsi0.0 \
    -qmp unix:/tmp/liwus_qmp.sock,server=on,wait=off \
    -serial stdio \
    -device qemu-xhci,id=xhci,p2=4,p3=4 \
    -device usb-ehci,id=ehci \
    -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0 \
    -netdev user,id=net0,restrict=off,hostfwd=tcp::2222-:2222,hostfwd=tcp::8080-:80 \
    -device rtl8139,netdev=net0 \
    -audiodev "$AUDIO_BACKEND,id=aud0" \
    -device intel-hda -device hda-duplex,audiodev=aud0
