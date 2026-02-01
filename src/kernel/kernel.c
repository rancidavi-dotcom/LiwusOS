#include "ata.h"
#include "book.h" // NOVO
#include "compositor.h"
#include "fat32.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "lgx.h"
#include "mouse.h"
#include "multiboot.h"
#include "net.h"
#include "panel.h" // NOVO
#include "pci.h"
#include "pmm.h"
#include "rtl8139.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "terminal.h"
#include "timer.h"
#include "vfs.h"
#include "video.h"
#include "wifi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool is_live_mode = true; // Definir global

extern uint32_t end;
extern void refresh_screen();
extern char get_last_key();
// extern void init_dock(); // Removido pois layer shell é controlado pelo
// compositor agora

void task_compositor_loop() {
  compositor_init();

  // Inicializa a UI do Sistema
  init_panel();
  init_terminal_app();
  init_book_app();

  int last_mx = -1, last_my = -1;
  bool last_clicked = false;

  while (1) {
    int mx = get_mouse_x();
    int my = get_mouse_y();
    bool clicked = is_left_clicked();
    char key = get_last_key();

    bool needs_update = (mx != last_mx || my != last_my || clicked != last_clicked || key != 0);

    if (needs_update) {
        // Input processing
        wl_handle_mouse(mx, my, clicked, false);

        if (clicked) {
          wl_surface_t *book_surf = get_book_surface(); 
          if (mx >= book_surf->x && mx <= book_surf->x + (int)book_surf->width &&
              my >= book_surf->y &&
              my <= book_surf->y + (int)book_surf->height + 40) {
            int rx = mx - book_surf->x;
            int ry = my - (book_surf->y + 40);
            if (ry >= 0) book_click_handler(rx, ry);
          }
        }

        if (key) {
          wl_handle_key(key);
          update_terminal_key(key);
        }

        extern bool check_win_key();
        extern void toggle_launcher();
        if (check_win_key()) {
          toggle_launcher();
        }

        compositor_repaint();
        
        last_mx = mx;
        last_my = my;
        last_clicked = clicked;
    } else {
        asm volatile("hlt"); // Descansa a CPU se nada mudou
    }
  }
}

void kernel_main(uint32_t magic, multiboot_info_t *mbi) {
  (void)magic;

  init_serial();
  serial_print("LiwusOS Kernel Booting...\n");

  init_gdt();
  init_idt();
  pmm_init((uint32_t)&end + 0x1000, mbi->mem_upper * 1024);
  init_video(mbi);

  pci_init();

  // Rede e Disco
  // pci_device_t* net = pci_get_net();
  // if (net) init_rtl8139(net);
  // fs_root = fat32_mount(ATA_PRIMARY, ATA_MASTER, 0);

  init_timer(100);
  init_tasking();
  init_timer(100);
  init_tasking();
  init_mouse();

// Initialize GPU (Overrides Video LFB if BGA present)
#include "gpu.h"
  init_gpu();

  // LGX Foundation Initialization (Backend for Compositor)
  lg_instance_t lgx_inst;
  lg_instance_create_info_t lgx_info = {"LiwusOS Wayland", 1};
  extern lg_device_t global_lg_device;
  extern lg_queue_t global_lg_queue;
  extern lg_command_pool_t global_lg_pool;
  extern lg_swapchain_t global_sw;

  if (lg_create_instance(&lgx_info, &lgx_inst) == LGX_SUCCESS) {
    uint32_t gpu_count = 0;
    lg_enumerate_physical_devices(lgx_inst, &gpu_count, NULL);
    if (gpu_count > 0) {
      lg_physical_device_t gpus[1];
      lg_enumerate_physical_devices(lgx_inst, &gpu_count, gpus);
      lg_create_device(gpus[0], &global_lg_device);
      lg_get_device_queue(global_lg_device, 0, 0, &global_lg_queue);
      lg_command_pool_create_info_t pool_info = {0};
      lg_create_command_pool(global_lg_device, &pool_info, &global_lg_pool);
      lg_swapchain_create_info_t sw_info = {screen_width, screen_height,
                                            LGX_FORMAT_B8G8R8A8_UNORM};
      lg_create_swapchain(global_lg_device, &sw_info, &global_sw);

      draw_string(10, 10, "LGX Backend initialized for Wayland Compositor",
                  0x00FF00);
    }
  }

  create_task(task_compositor_loop);
  extern void test_posix();
  create_task(test_posix);
  asm volatile("sti");
  while (1) {
    asm volatile("hlt");
  }
}