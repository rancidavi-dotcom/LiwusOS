#include "tcp.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "task.h"

// Interface com a shell do sistema
extern void exec_command_term(const char* cmd);
extern char* terminal_get_cwd(); // Precisamos dessa função no terminal.c

static tcp_socket_t* current_client = NULL;

void remote_shell_print_n(const char* msg, int n) {
    if (current_client && current_client->state == TCP_ESTABLISHED) {
        tcp_send(current_client, (uint8_t*)msg, n);
    }
}

void remote_shell_print(const char* msg) {
    if (msg) remote_shell_print_n(msg, strlen(msg));
}

void liwshd_loop() {
    serial_print("[liwshd] starting on port 2222...\n");
    tcp_socket_t* server = tcp_listen(2222);

    while (1) {
        while (server->state != TCP_ESTABLISHED) {
            switch_task();
        }

        current_client = server;
        serial_print("[liwshd] remote client connected!\n");

        remote_shell_print("\r\n--- Welcome to LiwusOS Remote Shell (v2.0) ---\r\n");
        remote_shell_print("[/house/localhost]# ");

        char cmd_buf[256];
        int cmd_ptr = 0;
        memset(cmd_buf, 0, sizeof(cmd_buf));

        while (server->state == TCP_ESTABLISHED) {
            uint8_t raw_buf[256];
            int received = tcp_receive(server, raw_buf, sizeof(raw_buf) - 1);
            
            if (received > 0) {
                for(int i=0; i<received; i++) {
                    uint8_t c = raw_buf[i];

                    if (c == 255) { i += 2; continue; } // Telnet IAC

                    if (c >= 32 && c <= 126) {
                        if (cmd_ptr < (int)sizeof(cmd_buf) - 1) {
                            cmd_buf[cmd_ptr++] = (char)c;
                            char echo[2] = {(char)c, '\0'};
                            remote_shell_print(echo);
                        }
                    } else if (c == '\r' || c == '\n') {
                        cmd_buf[cmd_ptr] = '\0';
                        remote_shell_print("\r\n");

                        if (cmd_ptr > 0) {
                            exec_command_term(cmd_buf);
                        }
                        
                        remote_shell_print("\r\n[/house/localhost]# ");
                        cmd_ptr = 0;
                        memset(cmd_buf, 0, sizeof(cmd_buf));
                    } else if (c == 8 || c == 127) {
                        if (cmd_ptr > 0) {
                            cmd_ptr--;
                            remote_shell_print("\b \b");
                        }
                    }
                }
            }
            switch_task();
        }

        serial_print("[liwshd] client disconnected, listening again...\n");
        current_client = NULL;
        // Reinicia o estado do socket para aceitar nova conexão
        server->state = TCP_LISTEN; 
    }
}
