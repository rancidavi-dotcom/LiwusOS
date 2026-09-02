#!/bin/bash
set -e

# =============================================================
# LiwusOS - build nativo + execucao (sem Docker)
# Requisitos: gcc, make, grub-mkrescue/xorriso (build do ISO),
#             qemu-system-i386 e qemu-img (execucao).
# =============================================================

die() { echo "ERRO: $*" >&2; exit 1; }
has() { command -v "$1" >/dev/null 2>&1; }

has gcc  || die "gcc nao encontrado. Instale: apt-get install build-essential"
has make || die "make nao encontrado. Instale: apt-get install make"

# ---- Build do kernel + ISO ----
# SKIP_BUILD=1 ./run.sh  -> usa o liwusos.iso que ja existe
if [ "${SKIP_BUILD:-0}" = "1" ]; then
    [ -f liwusos.iso ] || die "SKIP_BUILD=1 mas liwusos.iso nao existe."
else
    has grub-mkrescue || die "grub-mkrescue nao encontrado. Instale: apt-get install grub-pc-bin grub-common"
    has xorriso       || die "xorriso nao encontrado. Instale: apt-get install xorriso"
    has grub-file     || die "grub-file nao encontrado. Instale: apt-get install grub-common"
    echo "==> Compilando kernel + ISO (make all) ..."
    make all || die "Falha no build. Veja a saida acima."
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
# O som interno (AC'97) so chega aos alto-falantes do host se o QEMU
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

# ---- Execucao ----
# Usa a MESMA configuracao do "make run" (conhecida por funcionar):
# qemu-system-x86_64 + CD via -cdrom + disco persistente via AHCI.
# Adiciona: serial (log), SCSI (pendrive virtual), audio e rede.
exec qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -drive id=disk,file="$DISK_IMAGE",if=none,format=raw \
    -device ahci,id=ahci \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -blockdev "driver=raw,node-name=pen,file.driver=file,file.filename=$PWD/pendrive.img" \
    -device am53c974,id=scsi0 \
    -device scsi-hd,id=pendrive_disk,drive=pen,bus=scsi0.0 \
    -qmp unix:/tmp/liwus_qmp.sock,server=on,wait=off \
    -m 512 \
    -serial stdio \
    -audiodev "$AUDIO_BACKEND,id=aud0" \
    -device AC97,audiodev=aud0 \
    -net nic,model=rtl8139 \
    -net user,hostfwd=tcp::2222-:2222
