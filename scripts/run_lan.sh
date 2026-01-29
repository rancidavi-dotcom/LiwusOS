#!/bin/bash
# =============================================================
# run_lan.sh - prepara a rede LAN real (TAP) para o LiwusOS.
# Usado por 'make run-lan'.  Verifica o ambiente e cria o tap0
# quando possivel; caso contrario explica exatamente o que fazer.
# =============================================================

set -e

TAP="${LAN_TAP:-tap0}"

echo "==> [run-lan] Verificando capacidade de rede LAN real (TAP) ..."

# ---------- detecta WSL2 ----------
is_wsl=0
if [ -f /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    is_wsl=1
elif uname -r 2>/dev/null | grep -qi "microsoft"; then
    is_wsl=1
fi

# ---------- /dev/net/tun disponivel? ----------
have_tun=0
[ -c /dev/net/tun ] && have_tun=1

# ---------- tap N ja existe? ----------
tap_exists=0
if command -v ip >/dev/null 2>&1 && ip tuntap show 2>/dev/null | grep -q "^${TAP}"; then
    tap_exists=1
fi

if [ "$is_wsl" = "1" ]; then
    cat <<'EOF'

+----------------------------------------------------------------------+
|  ATENCAO: voce esta no WSL2.                                         |
|                                                                      |
|  No WSL2 a VM fica atras de um NAT proprio do Windows: um TAP criado |
|  dentro do WSL NAO chega na sua rede local. Para o LiwusOS aparecer  |
|  de verdade na LAN voce tem 2 caminhos:                             |
|                                                                      |
|  (A) Rodar o QEMU no WINDOWS nativo com um adaptador TAP bridged:   |
|      1. Instale o driver TAP (OpenVPN TAP-windows6).                |
|      2. No Control de Rede do Windows, selecione a placa Wi-Fi/Ether |
|         + o adaptador TAP -> botao direito -> 'Bridge Connections'. |
|      3. Rode o QEMU no Windows com:                                  |
|           -netdev tap,id=net0,ifname=TAP,script=no,downscript=no   |
|         (o mesmo liwus_disk.img funciona de ambos os lados).        |
|                                                                      |
|  (B) Ficar no WSL2 e usar port-forward (NAO e L2, mas funciona):   |
|      o 'make run' padrao ja encaminha tcp/2222 e tcp/8080.          |
|      Maquinas da LAN acessam o OS via IP do Windows:porta.          |
+----------------------------------------------------------------------+
EOF
    if [ "$have_tun" = "1" ] && [ "$tap_exists" = "1" ]; then
        echo "==> tap0 encontrado dentro do WSL2. Continuando com TAP."
        exit 0
    fi
    echo "==> Abortando 'run-lan' no WSL2 (sem TAP LAN real)."
    if [ "$have_tun" = "1" ]; then
        echo "==> Se quiser criar um tap para uso dentro do WSL (peer-to-peer):"
        echo "      sudo ip tuntap add dev ${TAP} mode tap && ip link set ${TAP} up"
    else
        echo "==> /dev/net/tun nao existe. Ative Virtual Machine Platform ou use o caminho (A)."
    fi
    exit 1
fi

# ---------- Linux nativo ----------
if [ "$have_tun" != "1" ]; then
    cat <<'EOF'
==> /dev/net/tun indisponivel. Crie o device:
      sudo mkdir -p /dev/net
      sudo mknod /dev/net/tun c 10 200
      sudo chmod 666 /dev/net/tun
EOF
    exit 1
fi

if [ "$tap_exists" = "1" ]; then
    echo "==> TAP ${TAP} ja existe. Usando."
    exit 0
fi

if [ "$(id -u)" != "0" ]; then
    cat <<'EOF'
==> Preciso de root para criar o tap0.
    Rode uma das opcoes (a 2a nao derruba a sua rede):
      sudo ./scripts/run_lan.sh   # cria tap0 automaticamente
    ou crie manualmente:
      sudo ip tuntap add dev tap0 mode tap
      sudo ip link set tap0 up
    e depois: make run-lan
EOF
    exit 1
fi

# ---------- cria tap0 + bridge (padrao: tap "pendurado" na LAN iface) ----------
lan_if="$(ip route show default 2>/dev/null | awk '{print $5; exit}')"
lan_ip="$(ip -4 -o addr show "${lan_if}" 2>/dev/null | awk '{print $4}' | head -1)"

echo "==> Criando ${TAP} ..."
ip tuntap add dev "${TAP}" mode tap
ip link set "${TAP}" up

if [ -n "$lan_if" ]; then
    cat <<EOF

TAP ${TAP} criado. Para pendura-lo na rede local use o bridge:

      sudo ip link add name br0 type bridge
      sudo ip link set ${lan_if} master br0
      sudo ip link set ${TAP}   master br0
      sudo ip link set br0 up
      sudo ip addr flush dev ${lan_if}
      sudo ip addr add ${lan_ip} dev br0    # IP da maquina vai para o bridge

ou, sem bridge (o OS so conversa com quem acessa este host):

      sudo ip addr add 10.13.37.2/24 dev ${TAP}
      ip addr add 10.13.37.1/24 dev ${TAP}   # host pode puxar um IP
      # roteamento para o OS: ip route ... (ajuste conforme a LAN)
EOF
else
    echo "==> Nao achei interface LAN para colocar ip no bridge."
fi
exit 0