#!/usr/bin/env python3
import socket
import subprocess
import time

def test_net():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    cmd = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "2048",
        "-display", "none", "-monitor", "none",
        "-serial", f"tcp:127.0.0.1:{port},server=on,wait=off",
        "-no-reboot", "-drive", "file=liwus_disk.img,format=raw,if=none,id=disk",
        "-device", "ahci,id=ahci", "-device", "ide-hd,drive=disk,bus=ahci.0",
        "-netdev", "user,id=net0,hostfwd=tcp::2323-:23", "-device", "e1000,netdev=net0",
        "-cdrom", "liwusos.iso",
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    sock = socket.create_connection(("127.0.0.1", port), timeout=5.0)
    sock.setblocking(False)

    buf = bytearray()
    start = time.time()
    sent_ifconfig = False
    sent_date = False
    sent_ping = False
    boot_time = 0

    while time.time() - start < 30.0:
        try:
            chunk = sock.recv(4096)
            if chunk:
                buf.extend(chunk)
                print(chunk.decode("utf-8", errors="replace"), end="", flush=True)

                if b"liwus>" in buf and not sent_ifconfig:
                    if boot_time == 0:
                        boot_time = time.time()
                    # Aguarda 2.5s para o DHCP responder
                    if time.time() - boot_time > 2.5:
                        print("\n[HOST -> LIWUS] Enviando: ifconfig\n")
                        sock.sendall(b"ifconfig\n")
                        sent_ifconfig = True
                        buf.clear()

                elif b"liwus>" in buf and sent_ifconfig and not sent_date:
                    time.sleep(0.5)
                    print("\n[HOST -> LIWUS] Enviando: date\n")
                    sock.sendall(b"date\n")
                    sent_date = True
                    buf.clear()

                elif b"liwus>" in buf and sent_date and not sent_ping:
                    time.sleep(0.5)
                    print("\n[HOST -> LIWUS] Enviando: ping 10.0.2.2 2\n")
                    sock.sendall(b"ping 10.0.2.2 2\n")
                    sent_ping = True
                    buf.clear()

                elif sent_ping and b"liwus>" in buf:
                    # Testa conexao Telnet na porta encaminhada 2323!
                    print("\n[TEST] Testando conexao Telnet na porta 2323...")
                    try:
                        t_sock = socket.create_connection(("127.0.0.1", 2323), timeout=3.0)
                        t_sock.settimeout(2.0)
                        banner = t_sock.recv(1024)
                        print(f"[TELNET RECEBIDO]:\n{banner.decode('utf-8', errors='replace')}")
                        def telnet_run(s_conn, cmd_bytes):
                            s_conn.sendall(cmd_bytes + b"\r\n")
                            accum = bytearray()
                            deadline = time.time() + 5.0
                            while time.time() < deadline:
                                try:
                                    c = s_conn.recv(1024)
                                    if c:
                                        accum.extend(c)
                                        if b"liwus>" in accum:
                                            break
                                except socket.timeout:
                                    break
                            return accum.decode("utf-8", errors="replace")

                        print(f"[TELNET RESPOSTA]:\n{telnet_run(t_sock, b'version')}")
                        print(f"[TELNET HELP]:\n{telnet_run(t_sock, b'help')}")
                        print(f"[TELNET UPTIME]:\n{telnet_run(t_sock, b'uptime')}")
                        print(f"[TELNET MEMINFO]:\n{telnet_run(t_sock, b'meminfo')}")
                        print(f"[TELNET PS]:\n{telnet_run(t_sock, b'ps')}")
                        t_sock.sendall(b"exit\r\n")
                        time.sleep(0.2)
                        t_sock.close()
                        print("[TEST] Conexao Telnet validada com sucesso!")
                    except Exception as e:
                        print(f"[TEST] Erro no teste Telnet: {e}")
                    break

        except BlockingIOError:
            time.sleep(0.1)

    sock.close()
    proc.terminate()
    proc.wait(timeout=2.0)

if __name__ == "__main__":
    test_net()
