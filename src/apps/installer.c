#include "installer.h"
#include "gui.h"
#include "io.h"
#include "mouse.h"
#include "string.h"
#include "task.h"
#include "video.h"
#include <stdbool.h>
#include <stddef.h>

volatile install_step_t step = STEP_WELCOME_DISK;
volatile int install_progress = 0;
static install_config_t config;

extern int fat32_format(int *progress);

void task_installer();

void start_installation() { create_task(task_installer); }

void task_installer() {
  step = STEP_FORMATTING;
  fat32_format((int *)&install_progress);

  step = STEP_COPYING;
  install_progress = 0;
  while (install_progress < 100) {
    install_progress++;
    for (volatile int i = 0; i < 500000; i++)
      ;
    switch_task();
  }

  step = STEP_DONE;
}

void open_installer() {
  step = STEP_WELCOME_DISK;
  install_progress = 0;
  memset(&config, 0, sizeof(install_config_t));
  strcpy(config.username, "davi");
  start_installation();
}

void draw_installer_full() {
  clear_screen(0x333333);

  int cx = 200;
  int cy = 200;
  int mx = get_mouse_x();
  int my = get_mouse_y();
  bool click = is_left_clicked();
  char k = get_last_key();

  switch ((install_step_t)step) {
  case STEP_WELCOME_DISK:
    draw_string(cx, cy, "Deseja instalar LiwusOS em qual disco?", 0xFFFFFF);
    draw_button_visual(cx, cy + 40, 400, 40, "/dev/hda (100MB QEMU HARDDISK)",
                       0x555555);
    if (click && is_inside(mx, my, cx, cy + 40, 400, 40)) {
      step = STEP_USERNAME;
    }
    break;

  case STEP_USERNAME:
    draw_string(cx, cy, "Escolha o nome de usuario:", 0xFFFFFF);
    size_t len = strlen(config.username);
    if (k >= 32 && k <= 126 && len < 30) {
      config.username[len] = k;
      config.username[len + 1] = '\0';
    } else if (k == '\b' && len > 0) {
      config.username[len - 1] = '\0';
    }
    draw_rect(cx, cy + 30, 300, 40, 0x1E1E28);
    draw_string(cx + 10, cy + 42, config.username, 0x00FF00);
    draw_button_visual(cx, cy + 100, 200, 40, "Proximo", 0x00A0A0);
    if (click && is_inside(mx, my, cx, cy + 100, 200, 40)) {
      step = STEP_CONFIRM;
    }
    break;

  case STEP_CONFIRM:
    draw_string(cx, cy,
                "Tem certeza que deseja instalar o LiwusOS no disco /dev/hda?",
                0xFFFFFF);
    draw_button_visual(cx, cy + 60, 100, 40, "Sim", 0x00AA00);
    if (click && is_inside(mx, my, cx, cy + 60, 100, 40)) {
      create_task(task_installer);
    }
    draw_button_visual(cx + 120, cy + 60, 100, 40, "Nao", 0xAA0000);
    if (click && is_inside(mx, my, cx + 120, cy + 60, 100, 40)) {
      step = STEP_WELCOME_DISK;
    }
    break;

  case STEP_FORMATTING:
    draw_string(cx, cy - 30, "Formatando o disco...", 0xFFFFFF);
    draw_loading_bar(cx, cy, 400, 30, install_progress);
    break;

  case STEP_COPYING:
    draw_string(cx, cy - 30, "Copiando arquivos do sistema...", 0xFFFFFF);
    draw_loading_bar(cx, cy, 400, 30, install_progress);
    break;

  case STEP_DONE:
    draw_string(cx, cy, "Instalacao Concluida!", 0x00FF00);
    draw_button_visual(cx, cy + 60, 200, 40, "Reiniciar Agora", 0xAA0000);
    if (click && is_inside(mx, my, cx, cy + 60, 200, 40)) {
      sys_reboot();
    }
    break;
  }
}