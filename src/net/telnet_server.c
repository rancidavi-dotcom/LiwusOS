#include "telnet_server.h"
#include "net_manager.h"
#include "ntp_client.h"
#include "http_client.h"
#include "serial.h"
#include "uapi/string.h"
#include "timer.h"
#include "pmm.h"
#include "task.h"
#include "kheap.h"
#include "sdfs.h"
#include "acpi.h"

/* lwIP Headers */
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"

#define TELNET_MAX_LINE 256

static struct tcp_pcb *g_telnet_listener = NULL;
static bool g_telnet_active = false;

/* Estado de uma conexao telnet */
typedef struct {
    struct tcp_pcb *pcb;
    char line_buf[TELNET_MAX_LINE];
    size_t line_len;
    int iac_state; /* 0: normal, 1: IAC recebido, 2: comando IAC recebido */
    char cmd_buf[TELNET_MAX_LINE];
    volatile bool has_command;
} telnet_session_t;

static telnet_session_t g_session;

static void telnet_send(struct tcp_pcb *pcb, const char *str) {
    if (!pcb || !str) return;
    size_t len = strlen(str);
    if (len == 0) return;

    err_t err = tcp_write(pcb, str, len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(pcb);
    }
}

static void telnet_send_prompt(struct tcp_pcb *pcb) {
    telnet_send(pcb, "liwus> ");
}

static void telnet_ping_output(const char *line, void *user_data) {
    struct tcp_pcb *pcb = (struct tcp_pcb *)user_data;
    if (pcb && line) {
        telnet_send(pcb, line);
        telnet_send(pcb, "\r\n");
    }
}

static void telnet_http_output(const char *line, void *user_data) {
    struct tcp_pcb *pcb = (struct tcp_pcb *)user_data;
    if (pcb && line) {
        telnet_send(pcb, line);
        telnet_send(pcb, "\r\n");
    }
}

static void telnet_handle_command(telnet_session_t *s, char *cmd_line) {
    /* Remove espacos iniciais */
    while (*cmd_line == ' ') cmd_line++;
    if (*cmd_line == '\0') {
        telnet_send_prompt(s->pcb);
        return;
    }

    /* Divide comando em argumentos */
    char *argv[16];
    int argc = 0;
    char *p = cmd_line;
    while (*p && argc < 16) {
        while (*p == ' ') *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }

    if (argc == 0) {
        telnet_send_prompt(s->pcb);
        return;
    }

    const char *cmd = argv[0];

    if (strcmp(cmd, "help") == 0) {
        telnet_send(s->pcb, "Comandos disponiveis no LiwusOS Telnet:\r\n");
        telnet_send(s->pcb, "  help          - Exibe esta mensagem de ajuda\r\n");
        telnet_send(s->pcb, "  version       - Exibe a versao do LiwusOS e kernel\r\n");
        telnet_send(s->pcb, "  date          - Exibe data e hora (sincronizada via NTP)\r\n");
        telnet_send(s->pcb, "  ntp [ip]      - Sincroniza o relogio com servidor NTP\r\n");
        telnet_send(s->pcb, "  ifconfig      - Exibe configuracoes e estatisticas de rede\r\n");
        telnet_send(s->pcb, "  ping <ip> [c] - Envia pacotes ICMP Echo Request\r\n");
        telnet_send(s->pcb, "  nslookup <dom>- Consulta endereco IP de um dominio (DNS)\r\n");
        telnet_send(s->pcb, "  curl <url>    - Obtem conteudo de pagina web via HTTP/HTTPS\r\n");
        telnet_send(s->pcb, "  wget <url> [d]- Baixa arquivo via HTTP/HTTPS e salva no disco SDFS\r\n");
        telnet_send(s->pcb, "  uptime        - Exibe tempo de atividade do sistema\r\n");
        telnet_send(s->pcb, "  meminfo       - Exibe estatisticas de memoria RAM\r\n");
        telnet_send(s->pcb, "  ps            - Lista tarefas ativas no kernel\r\n");
        telnet_send(s->pcb, "  diskinfo      - Exibe espaco livre no disco SDFS\r\n");
        telnet_send(s->pcb, "  ls [caminho]  - Lista arquivos do sistema de arquivos\r\n");
        telnet_send(s->pcb, "  cat <arquivo> - Exibe o conteudo de um arquivo texto\r\n");
        telnet_send(s->pcb, "  echo <texto>  - Exibe texto de volta\r\n");
        telnet_send(s->pcb, "  reboot        - Reinicia o computador remotamente\r\n");
        telnet_send(s->pcb, "  exit / quit   - Encerra a sessao telnet\r\n");
    }
    else if (strcmp(cmd, "version") == 0) {
        telnet_send(s->pcb, "LiwusOS v0.1 (x86_64) - Lean Kernel com lwIP 2.2.0\r\n");
        telnet_send(s->pcb, "Driver de Rede Ativo: ");
        telnet_send(s->pcb, net_get_driver_name());
        telnet_send(s->pcb, "\r\n");
    }
    else if (strcmp(cmd, "date") == 0 || strcmp(cmd, "time") == 0) {
        char buf[64];
        ntp_get_formatted_time(buf, sizeof(buf), -3);
        telnet_send(s->pcb, buf);
        if (ntp_is_synced()) {
            telnet_send(s->pcb, " (sincronizado via NTP)\r\n");
        } else {
            telnet_send(s->pcb, " (relogio local RTC - digite 'ntp' para sincronizar)\r\n");
        }
    }
    else if (strcmp(cmd, "ntp") == 0) {
        const char *server = (argc >= 2) ? argv[1] : NULL;
        telnet_send(s->pcb, "Sincronizando relogio com a Internet (NTP UDP 123)...\r\n");
        int res = ntp_sync(server);
        if (res == 0) {
            char buf[64];
            ntp_get_formatted_time(buf, sizeof(buf), -3);
            telnet_send(s->pcb, "Sucesso! Data e Hora atualizadas: ");
            telnet_send(s->pcb, buf);
            telnet_send(s->pcb, "\r\n");
        } else {
            telnet_send(s->pcb, "Falha na sincronizacao NTP (timeout ou servidor inacessivel).\r\n");
        }
    }
    else if (strcmp(cmd, "ifconfig") == 0) {
        char ip[16], mask[16], gw[16], mac_str[32];
        net_get_ip_strings(ip, mask, gw);
        const uint8_t *mac = net_get_mac();
        char byte_hex[4];

        mac_str[0] = '\0';
        for (int i = 0; i < 6; i++) {
            byte_hex[0] = "0123456789abcdef"[(mac[i] >> 4) & 0xF];
            byte_hex[1] = "0123456789abcdef"[mac[i] & 0xF];
            byte_hex[2] = (i < 5) ? ':' : '\0';
            byte_hex[3] = '\0';
            strcat(mac_str, byte_hex);
        }

        uint32_t rx = 0, tx = 0;
        net_get_packet_counts(&rx, &tx);

        char num[16];
        telnet_send(s->pcb, "et0: Link: ");
        telnet_send(s->pcb, net_get_link_status() ? "UP" : "DOWN");
        telnet_send(s->pcb, " HWaddr ");
        telnet_send(s->pcb, mac_str);
        telnet_send(s->pcb, "\r\n     inet addr: ");
        telnet_send(s->pcb, ip);
        telnet_send(s->pcb, "  Mask: ");
        telnet_send(s->pcb, mask);
        telnet_send(s->pcb, "  Gateway: ");
        telnet_send(s->pcb, gw);
        telnet_send(s->pcb, "\r\n     RX packets: ");
        int_to_str((int)rx, num);
        telnet_send(s->pcb, num);
        telnet_send(s->pcb, " TX packets: ");
        int_to_str((int)tx, num);
        telnet_send(s->pcb, num);
        telnet_send(s->pcb, "\r\n");
    }
    else if (strcmp(cmd, "ping") == 0) {
        if (argc < 2) {
            telnet_send(s->pcb, "uso: ping <IP> [quantidade]\r\n");
        } else {
            uint32_t count = 4;
            if (argc >= 3) {
                int c = 0;
                for (const char *ch = argv[2]; *ch >= '0' && *ch <= '9'; ch++) {
                    c = c * 10 + (*ch - '0');
                }
                if (c > 0) count = (uint32_t)c;
            }
            net_ping_custom(argv[1], count, telnet_ping_output, s->pcb);
        }
    }
    else if (strcmp(cmd, "uptime") == 0) {
        uint32_t total_sec = timer_ticks / 100;
        uint32_t h = total_sec / 3600;
        uint32_t m = (total_sec % 3600) / 60;
        uint32_t sec = total_sec % 60;
        char num[16];

        telnet_send(s->pcb, "Uptime: ");
        int_to_str(h, num); telnet_send(s->pcb, num); telnet_send(s->pcb, "h ");
        int_to_str(m, num); telnet_send(s->pcb, num); telnet_send(s->pcb, "m ");
        int_to_str(sec, num); telnet_send(s->pcb, num); telnet_send(s->pcb, "s (");
        int_to_str(timer_ticks, num); telnet_send(s->pcb, num);
        telnet_send(s->pcb, " ticks de timer a 100 Hz)\r\n");
    }
    else if (strcmp(cmd, "meminfo") == 0) {
        uint64_t total = pmm_get_total_memory();
        uint64_t used = pmm_get_used_memory();
        uint64_t free_mem = (total > used) ? (total - used) : 0;
        char num[16];

        telnet_send(s->pcb, "Informacoes de Memoria:\r\n");
        telnet_send(s->pcb, "  Total: ");
        int_to_str((int)(total / (1024 * 1024)), num); telnet_send(s->pcb, num); telnet_send(s->pcb, " MB\r\n");
        telnet_send(s->pcb, "  Usada: ");
        int_to_str((int)(used / (1024 * 1024)), num); telnet_send(s->pcb, num); telnet_send(s->pcb, " MB\r\n");
        telnet_send(s->pcb, "  Livre: ");
        int_to_str((int)(free_mem / (1024 * 1024)), num); telnet_send(s->pcb, num); telnet_send(s->pcb, " MB\r\n");
    }
    else if (strcmp(cmd, "ps") == 0) {
        task_info_t tasks[32];
        int count = task_snapshot(tasks, 32);
        char num[16];

        telnet_send(s->pcb, "PID  PRIO  ESTADO      SWITCH  CPU     NOME\r\n");
        for (int i = 0; i < count; i++) {
            int_to_str(tasks[i].id, num);
            telnet_send(s->pcb, num);
            telnet_send(s->pcb, (tasks[i].id < 10) ? "    " : "   ");

            int_to_str(tasks[i].priority, num);
            telnet_send(s->pcb, num);
            telnet_send(s->pcb, "     ");

            telnet_send(s->pcb, task_state_name(tasks[i].state));
            telnet_send(s->pcb, "   ");

            int_to_str((int)tasks[i].switch_count, num);
            telnet_send(s->pcb, num);
            telnet_send(s->pcb, "   ");

            int_to_str((int)tasks[i].cpu_ticks, num);
            telnet_send(s->pcb, num);
            telnet_send(s->pcb, "   ");

            telnet_send(s->pcb, tasks[i].name);
            telnet_send(s->pcb, "\r\n");
        }
    }
    else if (strcmp(cmd, "diskinfo") == 0) {
        uint32_t total = 0, used = 0;
        sdfs_get_usage(&total, &used);
        char num[16];

        telnet_send(s->pcb, "Disco SDFS:\r\n");
        telnet_send(s->pcb, "  Total: ");
        int_to_str((total * 4) / 1024, num); telnet_send(s->pcb, num); telnet_send(s->pcb, " MB\r\n");
        telnet_send(s->pcb, "  Usado: ");
        int_to_str((used * 4) / 1024, num); telnet_send(s->pcb, num); telnet_send(s->pcb, " MB\r\n");
        telnet_send(s->pcb, "  Livre: ");
        int_to_str(((total - used) * 4) / 1024, num); telnet_send(s->pcb, num); telnet_send(s->pcb, " MB\r\n");
    }
    else if (strcmp(cmd, "ls") == 0) {
        const char *path = (argc >= 2) ? argv[1] : "/";
        char name[64];
        uint32_t size = 0;
        int is_dir = 0;
        int idx = 0;
        char num[16];

        telnet_send(s->pcb, "Conteudo de ");
        telnet_send(s->pcb, path);
        telnet_send(s->pcb, ":\r\n");

        while (sdfs_list_dir_entry(path, idx++, name, &is_dir, &size) == 0) {
            if (is_dir) {
                telnet_send(s->pcb, "  [DIR]  ");
                telnet_send(s->pcb, name);
                telnet_send(s->pcb, "/\r\n");
            } else {
                telnet_send(s->pcb, "  [FILE] ");
                telnet_send(s->pcb, name);
                telnet_send(s->pcb, " (");
                int_to_str((int)size, num);
                telnet_send(s->pcb, num);
                telnet_send(s->pcb, " bytes)\r\n");
            }
        }
    }
    else if (strcmp(cmd, "cat") == 0) {
        if (argc < 2) {
            telnet_send(s->pcb, "uso: cat <arquivo>\r\n");
        } else {
            uint32_t size = 0;
            uint8_t *data = sdfs_read_file(argv[1], &size);
            if (data) {
                /* Converte quebras de linha para \r\n */
                for (uint32_t i = 0; i < size; i++) {
                    if (data[i] == '\n') {
                        telnet_send(s->pcb, "\r\n");
                    } else {
                        char c[2] = {(char)data[i], '\0'};
                        telnet_send(s->pcb, c);
                    }
                }
                telnet_send(s->pcb, "\r\n");
                kfree(data);
            } else {
                telnet_send(s->pcb, "cat: arquivo nao encontrado\r\n");
            }
        }
    }
    else if (strcmp(cmd, "nslookup") == 0) {
        if (argc < 2) {
            telnet_send(s->pcb, "uso: nslookup <dominio>\r\n");
        } else {
            char dns_srv[32];
            char res_ip[32];
            net_get_dns_server(dns_srv, sizeof(dns_srv));
            telnet_send(s->pcb, "Servidor DNS: ");
            telnet_send(s->pcb, dns_srv);
            telnet_send(s->pcb, "\r\n");
            int r = net_resolve_host(argv[1], res_ip, sizeof(res_ip));
            if (r == 0) {
                telnet_send(s->pcb, "Nome:     ");
                telnet_send(s->pcb, argv[1]);
                telnet_send(s->pcb, "\r\nEndereco: ");
                telnet_send(s->pcb, res_ip);
                telnet_send(s->pcb, "\r\n");
            } else {
                telnet_send(s->pcb, "nslookup: falha ao resolver ");
                telnet_send(s->pcb, argv[1]);
                telnet_send(s->pcb, "\r\n");
            }
        }
    }
    else if (strcmp(cmd, "curl") == 0) {
        if (argc < 2) {
            telnet_send(s->pcb, "uso: curl <URL>\r\n");
        } else {
            uint8_t *body = NULL;
            size_t size = 0;
            int status = 0;
            int r = http_get_mem(argv[1], &body, &size, &status, telnet_http_output, s->pcb);
            if (r >= 0 && body) {
                size_t plen = (size > 2048) ? 2048 : size;
                for (size_t i = 0; i < plen; i++) {
                    char ch = (char)body[i];
                    if (ch == '\r') continue;
                    if (ch == '\n') telnet_send(s->pcb, "\r\n");
                    else {
                        char cbuf[2] = { ch, '\0' };
                        telnet_send(s->pcb, cbuf);
                    }
                }
                if (size > plen) {
                    telnet_send(s->pcb, "\r\n[... truncado ...]\r\n");
                }
                telnet_send(s->pcb, "\r\n");
                kfree(body);
            } else {
                telnet_send(s->pcb, "curl: falha ao obter URL\r\n");
            }
        }
    }
    else if (strcmp(cmd, "wget") == 0) {
        if (argc < 2) {
            telnet_send(s->pcb, "uso: wget <URL> [caminho_destino_sdfs]\r\n");
        } else {
            const char *dest = (argc >= 3) ? argv[2] : NULL;
            http_download_file(argv[1], dest, telnet_http_output, s->pcb);
        }
    }
    else if (strcmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            telnet_send(s->pcb, argv[i]);
            if (i < argc - 1) telnet_send(s->pcb, " ");
        }
        telnet_send(s->pcb, "\r\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        telnet_send(s->pcb, "Reiniciando LiwusOS remotamente...\r\n");
        acpi_reboot();
    }
    else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        telnet_send(s->pcb, "Encerrando conexao telnet. Ate logo!\r\n");
        tcp_close(s->pcb);
        s->pcb = NULL;
        return;
    }
    else {
        telnet_send(s->pcb, "Comando desconhecido: ");
        telnet_send(s->pcb, cmd);
        telnet_send(s->pcb, ". Digite 'help' para comandos.\r\n");
    }

    telnet_send_prompt(s->pcb);
}

static err_t telnet_recv_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    telnet_session_t *s = &g_session;

    if (!p) {
        /* Conexao fechada pelo cliente remoto */
        serial_printf("[TELNET] Cliente desconectado.\n");
        tcp_close(pcb);
        s->pcb = NULL;
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);

    for (u16_t i = 0; i < p->tot_len; i++) {
        uint8_t b = pbuf_get_at(p, i);

        /* Maquina de estados para negociacao Telnet IAC (0xFF) */
        if (s->iac_state == 0) {
            if (b == 0xFF) {
                s->iac_state = 1; /* Inicio de sequencia IAC */
                continue;
            }
        } else if (s->iac_state == 1) {
            /* Segundo byte do IAC (ex: DO, DONT, WILL, WONT) */
            s->iac_state = 2;
            continue;
        } else if (s->iac_state == 2) {
            /* Terceiro byte do IAC (opcao) - ignora sequencia completa */
            s->iac_state = 0;
            continue;
        }

        /* Trata Enter (\r ou \n) */
        if (b == '\r') {
            telnet_send(pcb, "\r\n");
            s->line_buf[s->line_len] = '\0';
            strcpy(s->cmd_buf, s->line_buf);
            s->has_command = true;
            s->line_len = 0;
            s->line_buf[0] = '\0';
            continue;
        }
        if (b == '\n') {
            /* Se for \n avulso sem \r anterior */
            if (s->line_len > 0) {
                telnet_send(pcb, "\r\n");
                s->line_buf[s->line_len] = '\0';
                strcpy(s->cmd_buf, s->line_buf);
                s->has_command = true;
                s->line_len = 0;
                s->line_buf[0] = '\0';
            }
            continue;
        }

        /* Trata Backspace (0x08 ou 0x7F) */
        if (b == 0x08 || b == 0x7F) {
            if (s->line_len > 0) {
                s->line_len--;
                s->line_buf[s->line_len] = '\0';
                /* Envia sequencia backspace para apagar na tela do cliente */
                telnet_send(pcb, "\b \b");
            }
            continue;
        }

        /* Caractere imprimivel */
        if (b >= 32 && b <= 126) {
            if (s->line_len < (TELNET_MAX_LINE - 1)) {
                s->line_buf[s->line_len++] = (char)b;
                s->line_buf[s->line_len] = '\0';
                /* Eco local */
                char echo[2] = {(char)b, '\0'};
                telnet_send(pcb, echo);
            }
        }
    }

    pbuf_free(p);
    return ERR_OK;
}

static void telnet_err_callback(void *arg, err_t err) {
    (void)arg; (void)err;
    serial_printf("[TELNET] Erro na conexao TCP: %d\n", err);
    g_session.pcb = NULL;
}

static err_t telnet_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg; (void)err;
    if (!newpcb) return ERR_VAL;

    serial_printf("[TELNET] Nova conexao remota aceita!\n");

    g_session.pcb = newpcb;
    g_session.line_len = 0;
    g_session.line_buf[0] = '\0';
    g_session.iac_state = 0;

    tcp_arg(newpcb, &g_session);
    tcp_recv(newpcb, telnet_recv_callback);
    tcp_err(newpcb, telnet_err_callback);

    /* Envia banner de boas-vindas */
    telnet_send(newpcb, "\r\n");
    telnet_send(newpcb, "=======================================================\r\n");
    telnet_send(newpcb, "       _     _                          ___  ____      \r\n");
    telnet_send(newpcb, "      | |   (_)_      ___   _ ___      / _ \\/ ___|     \r\n");
    telnet_send(newpcb, "      | |   | \\ \\ /\\ / / | | / __|    | | | \\___ \\     \r\n");
    telnet_send(newpcb, "      | |___| |\\ V  V /| |_| \\__ \\    | |_| |___) |    \r\n");
    telnet_send(newpcb, "      |_____|_| \\_/\\_/  \\__,_|___/     \\___/|____/     \r\n");
    telnet_send(newpcb, "                                                       \r\n");
    telnet_send(newpcb, "     LIWUS OS - Remote Network Shell (Telnet Server)   \r\n");
    telnet_send(newpcb, "=======================================================\r\n");
    telnet_send(newpcb, "Conectado ao LiwusOS via rede local!\r\n");
    telnet_send(newpcb, "Digite 'help' para listar os comandos ou 'exit' para sair.\r\n\r\n");

    telnet_send_prompt(newpcb);
    return ERR_OK;
}

int telnet_server_init(uint16_t port) {
    if (g_telnet_active) return 0;

    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        serial_printf("[TELNET] Erro ao alocar TCP PCB.\n");
        return -1;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        serial_printf("[TELNET] Erro no bind na porta %u: %d\n", port, err);
        tcp_close(pcb);
        return -2;
    }

    g_telnet_listener = tcp_listen(pcb);
    if (!g_telnet_listener) {
        serial_printf("[TELNET] Erro no listen.\n");
        return -3;
    }

    tcp_accept(g_telnet_listener, telnet_accept_callback);
    g_telnet_active = true;

    serial_printf("[TELNET] Servidor Telnet ativo e escutando na porta TCP %u!\n", port);
    return 0;
}

void telnet_server_stop(void) {
    if (g_session.pcb) {
        tcp_close(g_session.pcb);
        g_session.pcb = NULL;
    }
    if (g_telnet_listener) {
        tcp_close(g_telnet_listener);
        g_telnet_listener = NULL;
    }
    g_telnet_active = false;
}

bool telnet_server_is_active(void) {
    return g_telnet_active;
}

void telnet_server_poll(void) {
    if (g_session.has_command && g_session.pcb) {
        g_session.has_command = false;
        telnet_handle_command(&g_session, g_session.cmd_buf);
    }
}
