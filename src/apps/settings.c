#include "settings.h"
#include "video.h"
#include "mouse.h"
#include "string.h"
#include "rtl8139.h"
#include "net.h"
#include "wifi.h"

typedef enum { TAB_INTERNET, TAB_SOUND, TAB_DISPLAY } settings_tab_t;
static settings_tab_t current_tab = TAB_DISPLAY;
widget_t* settings_win;

void on_tab_click(widget_t* self) {
    if (strcmp(self->text, "Internet") == 0) current_tab = TAB_INTERNET;
    else if (strcmp(self->text, "Som") == 0) current_tab = TAB_SOUND;
    else if (strcmp(self->text, "Tela") == 0) current_tab = TAB_DISPLAY;
}

widget_t* init_settings() {
    settings_win = create_window("Central de Controle", 250, 150, 600, 400);
    settings_win->visible = false;
    add_widget(settings_win, create_button("Internet", 10, 40, 120, 35, on_tab_click));
    add_widget(settings_win, create_button("Som", 10, 80, 120, 35, on_tab_click));
    add_widget(settings_win, create_button("Tela", 10, 120, 120, 35, on_tab_click));
    return settings_win;
}

void draw_settings_content() {
    if (!settings_win->visible) return;
    int cx = settings_win->x + 150; int cy = settings_win->y + 40;
    draw_rect(cx, cy, 430, 340, 0xEEEEEE);

    if (current_tab == TAB_INTERNET) {
        draw_string(cx + 20, cy + 20, "Rede e Internet", 0x000000);
        
        net_interface_t* netif = net_get_list();
        int y_off = 50;

        while(netif != NULL) {
            if (netif->type == NET_TYPE_ETHERNET) {
                draw_string(cx + 20, cy + y_off, "Ethernet (RTL8139): Conectado", 0x008800);
                y_off += 30;
            } else if (netif->type == NET_TYPE_WIFI && wifi_is_available()) {
                draw_string(cx + 20, cy + y_off, "Wi-Fi (Wireless Adapter):", 0x0000FF);
                y_off += 25;
                
                wifi_network_t nets[3];
                int count = wifi_scan(nets, 3);
                if (count == 0) {
                    draw_string(cx + 40, cy + y_off, "Nenhuma rede encontrada.", 0x777777);
                    y_off += 20;
                } else {
                    for(int i=0; i<count; i++) {
                        char net_str[64];
                        strcpy(net_str, "  - ");
                        strcat(net_str, nets[i].ssid);
                        draw_string(cx + 20, cy + y_off, net_str, 0x333333);
                        y_off += 20;
                    }
                }
                draw_string(cx + 20, cy + y_off, "Status: ", 0x000000);
                draw_string(cx + 100, cy + y_off, wifi_get_current_ssid(), 0x00AA00);
                y_off += 40;
            }
            netif = netif->next;
        }
        
        draw_button_visual(cx + 20, cy + 280, 200, 35, "Configurar IP", 0x444444);
    }
    else if (current_tab == TAB_SOUND) {
        draw_string(cx + 20, cy + 20, "Configuracoes de Audio", 0x000000);
    }
    else if (current_tab == TAB_DISPLAY) {
        draw_string(cx + 20, cy + 20, "Configuracoes de Tela", 0x000000);
        draw_string(cx + 20, cy + 60, "Resolucao Atual: ", 0x000000);
        char res_str[32];
        int_to_str(screen_width, res_str); strcat(res_str, "x");
        char h_str[16]; int_to_str(screen_height, h_str); strcat(res_str, h_str);
        draw_string(cx + 160, cy + 60, res_str, 0x0000AA);

        draw_rect(cx + 20, cy + 100, 380, 80, 0xFFDDDD); // Warning box
        draw_string(cx + 30, cy + 110, "AVISO: A alteracao de resolucao", 0xAA0000);
        draw_string(cx + 30, cy + 130, "foi bloqueada pelo sistema.", 0xAA0000);
        draw_string(cx + 30, cy + 150, "Operacao nao permitida nesta versao.", 0xAA0000);

        draw_button_visual(cx + 20, cy + 200, 200, 35, "Alterar (Bloqueado)", 0x888888);
    }
}
void open_settings() {
    if (!settings_win) init_settings();
    settings_win->visible = true;
    settings_win->focused = true;
}
