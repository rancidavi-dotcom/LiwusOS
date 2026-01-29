#include "terminal.h"
#include "video.h"
#include "mouse.h"
#include "string.h"
#include "io.h"
#include "pmm.h"
#include "timer.h"

widget_t* term_win;
static widget_t* lbl_output;
static widget_t* lbl_prompt;
static char input_buffer[64];
static int input_ptr = 0;

void exec_command(const char* cmd) {
    if (strcmp(cmd, "liwfetch") == 0) {
        static char fetch_info[2048];
        char tmp[32];
        
        uint32_t total_s = timer_ticks / 100;
        uint32_t mins = (total_s / 60) % 60;
        uint32_t hours = (total_s / 3600);
        
        uint32_t used_mb = pmm_get_used_memory() / 1024 / 1024;
        uint32_t total_mb = pmm_get_total_memory() / 1024 / 1024;

        strcpy(fetch_info, 
            "  _      _____ _    _ _    _  _____ \n"
            " | |    |_   _| |  | | |  | |/ ____|\n"
            " | |      | | | |  | | |  | | (___  \n"
            " | |      | | | |/\\| | |  | |\\___ \\ \n"
            " | |____ _| |_|  /\\  | |__| |____) |\n"
            " |______|_____|_/  \\__\\____/|_____/ \n"
            "                                    \n"
            " davi@liwusos\n"
            " ------------\n"
            " OS: LiwusOS x86\n"
            " Author: Davi VilasBoas Ranci\n"
            " Kernel: 1.0.3-RELEASE\n"
            " Uptime: ");
        
        int_to_str(hours, tmp); strcat(fetch_info, tmp); strcat(fetch_info, "h ");
        int_to_str(mins, tmp); strcat(fetch_info, tmp); strcat(fetch_info, "m\n");
        
        strcat(fetch_info, " `/++++/+++++++:     Resolution: ");
        int_to_str(screen_width, tmp); strcat(fetch_info, tmp); strcat(fetch_info, "x");
        int_to_str(screen_height, tmp); strcat(fetch_info, tmp); strcat(fetch_info, "\n");
        
        strcat(fetch_info, " `/+++++++++++++:    Memory: ");
        int_to_str(used_mb, tmp); strcat(fetch_info, tmp); strcat(fetch_info, "MiB / ");
        int_to_str(total_mb, tmp); strcat(fetch_info, tmp); strcat(fetch_info, "MiB\n");
        
        strcat(fetch_info, 
            " `+---+oooooooooo/`  \n"
            " .--+--++-------+`   \n"
            " .------+-------+`   \n"
            " -----------------   ");
            
        lbl_output->text = fetch_info;
    }
    else if (strcmp(cmd, "help") == 0) lbl_output->text = "Comandos: help, liwfetch, ping <url>, clear, ver, reboot";
    else if (strstr(cmd, "ping") == cmd) {
        if (strlen(cmd) <= 5) {
            lbl_output->text = "Erro: Digite a URL. Ex: ping google.com";
        } else {
            const char* host = cmd + 5;
            extern uint32_t net_resolve_host(const char* host);
            uint32_t ip = net_resolve_host(host);
            
            static char full_msg[1024];
            strcpy(full_msg, "Disparando contra ");
            strcat(full_msg, host);
            strcat(full_msg, " com 32 bytes de dados:\n");
            lbl_output->text = full_msg;
            refresh_screen();

            extern void netstack_send_ping(uint32_t dest_ip);
            extern volatile int ping_received;
            extern bool check_ctrl_c();

            for(int i = 0; i < 4; i++) {
                if (check_ctrl_c()) {
                    strcat(full_msg, "\nInterrompido por Ctrl+C.");
                    lbl_output->text = full_msg;
                    return;
                }

                ping_received = 0;
                netstack_send_ping(ip);
                
                // Espera pela resposta real ou timeout de 2 segundos
                uint32_t timeout = 20000000; 
                while(!ping_received && timeout--) {
                    asm volatile("nop");
                }

                if (ping_received) {
                    strcat(full_msg, "Resposta de ");
                    strcat(full_msg, host);
                    strcat(full_msg, ": bytes=32 tempo<1ms TTL=64\n");
                } else {
                    strcat(full_msg, "Esgotado o tempo de limite do pedido.\n");
                }
                
                lbl_output->text = full_msg;
                refresh_screen();

                // Pequeno intervalo entre os pings
                for(int j=0; j<10000000; j++) asm volatile("nop");
            }
            strcat(full_msg, "\nEstatisticas do Ping concluida.");
            lbl_output->text = full_msg;
        }
    }
    else if (strcmp(cmd, "clear") == 0) lbl_output->text = "Console Limpo.";
    else if (strcmp(cmd, "reboot") == 0) sys_reboot();
    else lbl_output->text = "Erro: Comando desconhecido.";
}

widget_t* init_terminal() {
    term_win = create_window("Terminal", 150, 150, 600, 400);
    
    /* Fundo Quase Preto (não 0x000000 para evitar chroma key) */
    widget_t* bg = create_label("", 0, 0, 0);
    bg->type = TYPE_BUTTON; bg->color = 0x050505; bg->w = 600; bg->h = 370;
    add_widget(term_win, bg);

    lbl_output = create_label("LiwusOS Shell. Digite 'liwfetch'.", 10, 10, 0xAAAAAA);
    add_widget(term_win, lbl_output);

    lbl_prompt = create_label("> ", 10, 300, 0x00FF00);
    add_widget(term_win, lbl_prompt);
    
    term_win->visible = false;
    return term_win;
}

void open_terminal() { term_win->visible = true; term_win->focused = true; }

void update_terminal_key(char k) {
    if (!term_win->visible || !term_win->focused) return;

    if (k > 0) {
        if (k == '\n') {
            input_buffer[input_ptr] = '\0';
            exec_command(input_buffer);
            input_ptr = 0; input_buffer[0] = '\0';
        } else if (k == '\b') {
            if (input_ptr > 0) input_buffer[--input_ptr] = '\0';
        } else if (input_ptr < 50) {
            input_buffer[input_ptr++] = k;
            input_buffer[input_ptr] = '\0';
        }
        
        static char display_line[128];
        strcpy(display_line, "> ");
        strcat(display_line, input_buffer);
        lbl_prompt->text = display_line;
        term_win->dirty = true; // Mark window for redraw
    }
}