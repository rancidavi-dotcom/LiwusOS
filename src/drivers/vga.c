#include "vga.h"
#include "io.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "vmm.h"
#include "kernel_layout.h"
#include "pci.h"
#include <stdbool.h>

extern void fast_memcpy(void *dst, const void *src, uint64_t size);

uint64_t vga_fb_addr = 0;
uint32_t vga_fb_width = 0;
uint32_t vga_fb_height = 0;
uint32_t vga_fb_pitch = 0;
uint8_t vga_fb_bpp = 0;
uint8_t vga_fb_type = 0;
uint8_t vga_fb_red_pos = 0;
uint8_t vga_fb_red_size = 0;
uint8_t vga_fb_green_pos = 0;
uint8_t vga_fb_green_size = 0;
uint8_t vga_fb_blue_pos = 0;
uint8_t vga_fb_blue_size = 0;

static bool is_framebuffer = false;
static uint32_t *vga_backbuffer = NULL;
static size_t vga_row = 0;
static size_t vga_column = 0;
static int font_width = 8;
static int font_height = 16;
static size_t max_cols = 80;
static size_t max_rows = 25;
static vga_display_mode_t display_modes[VGA_DISPLAY_MAX_MODES];
static size_t display_mode_count = 0;
static uint16_t display_max_x = 0, display_max_y = 0, display_vram_kb = 0;
static bool display_backend_ready = false;
static uint16_t display_gpu_vendor = 0, display_gpu_device = 0;

#define VBE_INDEX_PORT 0x01CE
#define VBE_DATA_PORT  0x01CF
#define VBE_XRES       0x01
#define VBE_YRES       0x02
#define VBE_BPP        0x03
#define VBE_ENABLE     0x04
#define VBE_VIDEO_MEM  0x0A
#define VBE_ID         0x00
#define VBE_GETCAPS    0x02
#define VBE_ENABLED    0x01
#define VBE_LFB        0x40
#define VBE_NOCLEARMEM 0x80

static uint16_t vbe_read(uint16_t index) {
    outw(VBE_INDEX_PORT, index);
    return inw(VBE_DATA_PORT);
}

static void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_INDEX_PORT, index);
    outw(VBE_DATA_PORT, value);
}

static int edid_checksum_valid(const uint8_t *block) {
    uint8_t sum = 0;
    for (int i = 0; i < 128; i++) sum = (uint8_t)(sum + block[i]);
    return sum == 0;
}

static void display_add_mode(uint16_t width, uint16_t height) {
    if (!display_backend_ready || width < 320 || height < 200 ||
        width > display_max_x || height > display_max_y || (width & 1) ||
        (uint64_t)width * height * 4 > (uint64_t)display_vram_kb * 1024)
        return;
    for (size_t i = 0; i < display_mode_count; i++)
        if (display_modes[i].width == width && display_modes[i].height == height) return;
    if (display_mode_count >= VGA_DISPLAY_MAX_MODES) return;
    size_t at = display_mode_count++;
    while (at && (uint32_t)display_modes[at - 1].width * display_modes[at - 1].height >
                  (uint32_t)width * height) {
        display_modes[at] = display_modes[at - 1];
        at--;
    }
    display_modes[at].width = width;
    display_modes[at].height = height;
}

static void edid_add_detailed(const uint8_t *d) {
    uint32_t clock = (uint32_t)d[0] | ((uint32_t)d[1] << 8);
    if (!clock) return;
    uint16_t h = (uint16_t)(d[2] | ((d[4] & 0xF0) << 4));
    uint16_t v = (uint16_t)(d[5] | ((d[7] & 0xF0) << 4));
    display_add_mode(h, v);
}

static void edid_parse_base(const uint8_t *edid) {
    static const uint16_t established[][2] = {
        {720,400},{720,400},{640,480},{640,480},{640,480},{640,480},
        {800,600},{800,600},{800,600},{800,600},{832,624},{1024,768},
        {1024,768},{1024,768},{1024,768},{1280,1024},{1152,870}
    };
    static const uint8_t bitpos[] = {7,6,5,4,3,2,1,0};
    for (int b = 0; b < 3; b++) {
        for (int bit = 0; bit < 8; bit++)
            if (edid[35 + b] & (1u << bitpos[bit])) {
                int idx = b * 8 + bit;
                if (idx < (int)(sizeof(established) / sizeof(established[0])))
                    display_add_mode(established[idx][0], established[idx][1]);
            }
    }
    for (int i = 0; i < 8; i++) {
        uint8_t a = edid[38 + i * 2], b = edid[39 + i * 2];
        if (a == 1 && b == 1) continue;
        uint16_t w = (uint16_t)(a * 8 + 248);
        uint8_t aspect = b >> 6;
        uint16_t h = aspect == 0 ? (uint16_t)(w * 10 / 16) :
                     aspect == 1 ? (uint16_t)(w * 3 / 4) :
                     aspect == 2 ? (uint16_t)(w * 4 / 5) : (uint16_t)(w * 9 / 16);
        display_add_mode(w, h);
    }
    for (int i = 0; i < 4; i++) edid_add_detailed(edid + 54 + i * 18);
}

static void edid_parse_cta(const uint8_t *block) {
    static const uint16_t vic[][2] = {
        {0,0},{640,480},{720,480},{720,480},{1280,720},{1920,1080},
        {720,480},{720,480},{720,240},{720,240},{2880,480},{2880,480},
        {2880,240},{2880,240},{1440,480},{1440,480},{1920,1080},
        {720,576},{720,576},{1280,720},{1920,1080},{720,576},{720,576},
        {720,288},{720,288},{2880,576},{2880,576},{2880,288},{2880,288},
        {1440,576},{1440,576},{1920,1080},{1920,1080},{1920,1080},
        {1920,1080},{1920,1080},{1920,1080},{1920,1080},{1920,1080},
        {1920,1080},{1920,1080},{1280,720},{1280,720},{1280,720},
        {1280,720},{1280,720},{1280,720},{1280,720},{1280,720},
        {1280,720},{1280,720},{720,576},{720,576},{720,576},{720,576},
        {720,576},{720,576},{720,576},{720,576},{720,576},{720,576},
        {720,480},{720,480},{720,480},{720,480},{720,480},{720,480},
        {720,480},{720,480},{720,480},{720,480},{1920,1080},{1920,1080},
        {1920,1080},{1920,1080},{1920,1080},{1920,1080},{1920,1080},
        {1920,1080},{1920,1080},{1920,1080},{1920,1080},{1920,1080},
        {1920,1080},{1920,1080},{1920,1080},{1920,1080},{1920,1080},
        {3840,2160},{3840,2160},{3840,2160},{3840,2160},{3840,2160},
        {3840,2160},{3840,2160},{3840,2160},{3840,2160},{3840,2160},
        {4096,2160},{4096,2160},{4096,2160},{4096,2160},{4096,2160},
        {4096,2160},{4096,2160},{4096,2160},{4096,2160},{4096,2160}
    };
    uint8_t end = block[2];
    if (end < 4 || end > 127) end = 127;
    for (uint8_t p = 4; p < end;) {
        uint8_t header = block[p++];
        uint8_t tag = header >> 5, len = header & 0x1F;
        if ((uint16_t)p + len > end) break;
        if (tag == 2) {
            for (uint8_t i = 0; i < len; i++) {
                uint8_t code = block[p + i] & 0x7F;
                if (code && code < sizeof(vic) / sizeof(vic[0]))
                    display_add_mode(vic[code][0], vic[code][1]);
            }
        }
        p = (uint8_t)(p + len);
    }
    for (uint8_t p = end; p + 18 <= 127; p += 18) edid_add_detailed(block + p);
}

bool vga_display_backend_ready(void) { return display_backend_ready; }

int vga_display_adapter_ids(uint16_t *vendor_id, uint16_t *device_id) {
    if (!display_gpu_vendor || !display_gpu_device) return 0;
    if (vendor_id) *vendor_id = display_gpu_vendor;
    if (device_id) *device_id = display_gpu_device;
    return 1;
}

int vga_display_modes(vga_display_mode_t *out, size_t capacity) {
    if (!display_backend_ready || !out || !capacity) return -1;
    size_t n = display_mode_count < capacity ? display_mode_count : capacity;
    memcpy(out, display_modes, n * sizeof(*out));
    return (int)n;
}

static void vbe_restore_mode(uint16_t x, uint16_t y, uint16_t bpp, uint16_t enabled) {
    vbe_write(VBE_ENABLE, 0);
    vbe_write(VBE_XRES, x);
    vbe_write(VBE_YRES, y);
    vbe_write(VBE_BPP, bpp);
    vbe_write(VBE_ENABLE, enabled);
}

int vga_display_set_mode(uint16_t width, uint16_t height) {
    bool advertised = false;
    for (size_t i = 0; i < display_mode_count; i++)
        if (display_modes[i].width == width && display_modes[i].height == height) advertised = true;
    if (!display_backend_ready || !advertised || width > display_max_x || height > display_max_y) return -1;
    uint32_t pitch = (uint32_t)width * 4U;
    uint64_t size = (uint64_t)pitch * height;
    uint32_t *new_back = (uint32_t *)kmalloc((size_t)size);
    if (!new_back) return -2;
    memset(new_back, 0, (size_t)size);

    uint16_t old_x = vbe_read(VBE_XRES), old_y = vbe_read(VBE_YRES);
    uint16_t old_bpp = vbe_read(VBE_BPP), old_en = vbe_read(VBE_ENABLE);
    vbe_restore_mode(width, height, 32, VBE_ENABLED | VBE_LFB | VBE_NOCLEARMEM);
    uint16_t actual_x = vbe_read(VBE_XRES), actual_y = vbe_read(VBE_YRES);
    if (vga_fb_addr == 0) {
        pci_device_t *gpu = pci_get_gpu();
        if (gpu) {
            uint32_t bar0 = pci_read_config(gpu->bus, gpu->device, gpu->function, 0x10);
            vga_fb_addr = (uint64_t)(bar0 & ~0xFULL);
        }
    }
    if ((actual_x != width && !(width == 1366 && actual_x == 1360)) ||
        actual_y != height || vga_fb_addr == 0) {
        vbe_restore_mode(old_x, old_y, old_bpp, old_en);
        kfree(new_back);
        return -3;
    }
    width = actual_x;
    pitch = (uint32_t)width * 4U;

    vmm_map_framebuffer(vga_fb_addr, size);
    uint32_t *old_back = vga_backbuffer;
    vga_fb_width = width;
    vga_fb_height = height;
    vga_fb_pitch = pitch;
    vga_fb_bpp = 32;
    vga_fb_type = 1;
    vga_fb_red_pos = 16;
    vga_fb_red_size = 8;
    vga_fb_green_pos = 8;
    vga_fb_green_size = 8;
    vga_fb_blue_pos = 0;
    vga_fb_blue_size = 8;
    is_framebuffer = true;
    vga_backbuffer = new_back;
    max_cols = width / (size_t)font_width;
    max_rows = height / (size_t)font_height;
    vga_row = vga_column = 0;
    if (old_back) kfree(old_back);
    vga_clear_framebuffer(0x00111A24U);
    vga_flip();
    return 0;
}

void vga_display_init(void) {
    display_backend_ready = false;
    display_mode_count = 0;
    pci_device_t *gpu = pci_get_gpu();
    display_gpu_vendor = gpu ? gpu->vendor_id : 0;
    display_gpu_device = gpu ? gpu->device_id : 0;
    if (gpu) {
        serial_print("[display] PCI adapter ");
        serial_print_hex(gpu->vendor_id);
        serial_print(":");
        serial_print_hex(gpu->device_id);
        serial_print(" at ");
        serial_print_hex(gpu->bus);
        serial_print(":");
        serial_print_hex(gpu->device);
        serial_print(".");
        serial_print_hex(gpu->function);
        serial_print("\n");
    } else {
        serial_print("[display] no PCI display adapter found\n");
    }

    /* 1. Testa portas I/O Bochs/VBE DISPI (0x1CE / 0x1CF) presentes em QEMU, VirtualBox, Bochs */
    uint16_t id = vbe_read(VBE_ID);
    if (id >= 0xB0C0 && id <= 0xB0C5) {
        uint16_t enable = vbe_read(VBE_ENABLE);
        vbe_write(VBE_ENABLE, VBE_GETCAPS);
        display_max_x = vbe_read(VBE_XRES);
        display_max_y = vbe_read(VBE_YRES);
        uint16_t max_bpp = vbe_read(VBE_BPP);
        display_vram_kb = (uint16_t)(vbe_read(VBE_VIDEO_MEM) * 64U);
        if (!display_vram_kb) {
            display_vram_kb = 16384; /* Fallback: pelo menos 16 MB para QEMU/VirtualBox */
        }
        vbe_write(VBE_ENABLE, enable);

        if (gpu) {
            uint32_t bar0 = pci_read_config(gpu->bus, gpu->device, gpu->function, 0x10);
            uint64_t gpu_fb_phys = (uint64_t)(bar0 & ~0xFULL);
            if (!vga_fb_addr && gpu_fb_phys) {
                vga_fb_addr = gpu_fb_phys;
            }
        }

        if (max_bpp >= 32 && display_max_x >= 640 && display_max_y >= 480) {
            display_backend_ready = true;
            serial_print("[display] Bochs/VBE DISPI ativo (ID=0x");
            serial_print_hex(id);
            serial_print(")\n");
        }
    }

    /* 2. Se a GPU for QEMU (0x1234:0x1111) com MMIO BAR2, le os modos anunciados no EDID */
    if (gpu && gpu->vendor_id == 0x1234 && gpu->device_id == 0x1111) {
        uint16_t command = (uint16_t)pci_read_config(gpu->bus, gpu->device, gpu->function, 0x04);
        pci_write_config(gpu->bus, gpu->device, gpu->function, 0x04, command | 0x0003);
        uint32_t bar_low = pci_read_config(gpu->bus, gpu->device, gpu->function, 0x18);
        if (!(bar_low & 1) && (bar_low & 6) == 0) {
            uint64_t edid_phys = (uint64_t)(bar_low & ~0xFULL);
            if (bar_low & 4) edid_phys |= (uint64_t)pci_read_config(gpu->bus, gpu->device, gpu->function, 0x1C) << 32;
            if (edid_phys && edid_phys <= DIRECT_MAP_SIZE - 4096) {
                vmm_map_framebuffer(edid_phys, 4096);
                volatile const uint8_t *mmio = (volatile const uint8_t *)phys_to_virt(edid_phys);
                static uint8_t edid[4 * 128];
                for (size_t i = 0; i < sizeof(edid); i++) edid[i] = mmio[i];
                static const uint8_t signature[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
                if (!memcmp(edid, signature, sizeof(signature)) && edid_checksum_valid(edid)) {
                    edid_parse_base(edid);
                    uint8_t extensions = edid[126];
                    if (extensions > 3) extensions = 3;
                    for (uint8_t i = 1; i <= extensions; i++) {
                        const uint8_t *block = edid + (size_t)i * 128;
                        if (edid_checksum_valid(block) && block[0] == 0x02) edid_parse_cta(block);
                    }
                }
            }
        }
    }

    if (display_backend_ready) {
        display_add_mode(1366, 768);
        display_add_mode(1360, 768);
        display_add_mode(1920, 1080);
        display_add_mode(1600, 900);
        display_add_mode(1280, 720);
        display_add_mode(1024, 768);
        display_add_mode(800, 600);
        display_add_mode(640, 480);
        display_add_mode((uint16_t)vga_fb_width, (uint16_t)vga_fb_height);
    }
    if (!display_mode_count) {
        display_backend_ready = false;
        serial_print("[display] backend VBE indisponivel; mantendo resolucao nativa do GRUB\n");
        return;
    }
    serial_print("[display] Modos graficos disponiveis: ");
    char count_buf[16]; itoa((int)display_mode_count, count_buf, 10); serial_print(count_buf);
    serial_print(" modos\n");
}

// Fallback text mode buffer
static uint16_t *text_buffer;

static uint8_t vga_color = 0;
static uint32_t vga_fg_color = 0xFFFFFF; // White
static uint32_t vga_bg_color = 0x000000; // Black

// Font handling (PSF1)
extern uint8_t _binary_src_drivers_font_psf_start[];
extern char _binary_src_drivers_font_psf_end;
static uint8_t* font_data = NULL;
static int font_bpg = 16; // bytes per glyph

// Columns and rows available

// ANSI escape sequence parser state
static int vga_esc_state = 0; 
static int vga_esc_params[16];
static int vga_esc_pcount;
static int vga_esc_val;

static int vga_ansi_fg = 7;
static int vga_ansi_bg = 0;
static bool vga_reverse = false;

static const uint32_t vga_palette[16] = {
    0xCC0B0F19, // 0: Black (Glassmorphic Midnight Slate: 80% opacity)
    0xFF3B82F6, // 1: Blue (Vibrant Indigo/Blue 500)
    0xFF10B981, // 2: Emerald Green 500
    0xFF06B6D4, // 3: Cyan 500
    0xFFEF4444, // 4: Rose/Red 500
    0xFFD946EF, // 5: Fuchsia 500
    0xFFF59E0B, // 6: Amber 500
    0xFFCBD5E1, // 7: Light Grey (Slate 300)
    0xFF64748B, // 8: Dark Grey (Slate 500)
    0xFF60A5FA, // 9: Light Blue 400
    0xFF34D399, // 10: Light Emerald 400
    0xFF22D3EE, // 11: Light Cyan 400
    0xFFF87171, // 12: Light Red 400
    0xFFE879F9, // 13: Light Fuchsia 400
    0xFFFBBF24, // 14: Light Amber 400
    0xFFF8FAFC, // 15: White (Slate 50)
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

static inline uint32_t vga_convert_color(uint32_t color) {
    uint32_t red = (color >> 16) & 0xFF;
    uint32_t green = (color >> 8) & 0xFF;
    uint32_t blue = color & 0xFF;
    return (red << vga_fb_red_pos) | (green << vga_fb_green_pos) |
           (blue << vga_fb_blue_pos);
}

void vga_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!is_framebuffer) return;
    if (x >= vga_fb_width || y >= vga_fb_height) return;
    uint32_t px = vga_convert_color(color);
    uint32_t stride_words = vga_fb_pitch / 4;
    uint32_t offset = y * stride_words + x;
    uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);
    fb[offset] = px;
    if (vga_backbuffer) {
        vga_backbuffer[offset] = px;
    }
}

void vga_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!is_framebuffer) return;
    if (x >= vga_fb_width || y >= vga_fb_height) return;
    if (x + w > vga_fb_width) w = vga_fb_width - x;
    if (y + h > vga_fb_height) h = vga_fb_height - y;
    if (!w || !h) return;

    uint32_t px = vga_convert_color(color);
    uint32_t stride_words = vga_fb_pitch / 4;
    uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);
    uint32_t* dst = fb + (y * stride_words) + x;
    uint32_t* back_dst = vga_backbuffer ? (vga_backbuffer + (y * stride_words) + x) : NULL;

    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            dst[i] = px;
        }
        if (back_dst) {
            for (uint32_t i = 0; i < w; i++) {
                back_dst[i] = px;
            }
            back_dst += stride_words;
        }
        dst += stride_words;
    }
}

static void vga_draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (!is_framebuffer || !font_data) return;
    if (x + (uint32_t)font_width > vga_fb_width || y + (uint32_t)font_height > vga_fb_height) return;

    uint8_t* glyph = font_data + (unsigned char)c * font_bpg;
    uint32_t fg_px = vga_convert_color(fg);
    uint32_t bg_px = vga_convert_color(bg);
    uint32_t stride_words = vga_fb_pitch / 4;
    uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);
    uint32_t* dst = fb + (y * stride_words) + x;
    uint32_t* back_dst = vga_backbuffer ? (vga_backbuffer + (y * stride_words) + x) : NULL;

    for (int j = 0; j < font_height; j++) {
        uint8_t row = glyph[j];
        uint32_t p0 = (row & 0x80) ? fg_px : bg_px;
        uint32_t p1 = (row & 0x40) ? fg_px : bg_px;
        uint32_t p2 = (row & 0x20) ? fg_px : bg_px;
        uint32_t p3 = (row & 0x10) ? fg_px : bg_px;
        uint32_t p4 = (row & 0x08) ? fg_px : bg_px;
        uint32_t p5 = (row & 0x04) ? fg_px : bg_px;
        uint32_t p6 = (row & 0x02) ? fg_px : bg_px;
        uint32_t p7 = (row & 0x01) ? fg_px : bg_px;

        dst[0] = p0; dst[1] = p1; dst[2] = p2; dst[3] = p3;
        dst[4] = p4; dst[5] = p5; dst[6] = p6; dst[7] = p7;

        if (back_dst) {
            back_dst[0] = p0; back_dst[1] = p1; back_dst[2] = p2; back_dst[3] = p3;
            back_dst[4] = p4; back_dst[5] = p5; back_dst[6] = p6; back_dst[7] = p7;
            back_dst += stride_words;
        }
        dst += stride_words;
    }
}

static void vga_update_color(void) {
    int fg = vga_ansi_fg;
    int bg = vga_ansi_bg;
    if (vga_reverse) {
        int tmp = fg;
        fg = bg;
        bg = tmp;
    }
    vga_color = vga_entry_color((enum vga_color)fg, (enum vga_color)bg);
    vga_fg_color = vga_palette[fg];
    vga_bg_color = vga_palette[bg];
}

static void vga_sgr_reset(void) {
    vga_ansi_fg = 7;
    vga_ansi_bg = 0;
    vga_reverse = false;
    vga_update_color();
}

static void vga_exec_esc(void) {
    // simplified for brevity... (same logic as before, using max_rows/max_cols)
    if (vga_esc_pcount == 0) {
        vga_esc_params[0] = 0;
        vga_esc_pcount = 1;
    }
    char cmd = (char)vga_esc_val;
    if (cmd == 'H' || cmd == 'f') {
        int row = vga_esc_params[0] - 1;
        int col = (vga_esc_pcount > 1 ? vga_esc_params[1] : 1) - 1;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        if ((size_t)row >= max_rows) row = max_rows - 1;
        if ((size_t)col >= max_cols) col = max_cols - 1;
        vga_row = (size_t)row;
        vga_column = (size_t)col;
        vga_set_cursor((int)vga_column, (int)vga_row);
    } else if (cmd == 'J') {
        int mode = vga_esc_params[0];
        if (mode == 2) {
            vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
    } else if (cmd == 'C') {
        int n = vga_esc_params[0];
        if (n == 0) n = 1;
        vga_column += n;
        if (vga_column >= max_cols) vga_column = max_cols - 1;
        vga_set_cursor((int)vga_column, (int)vga_row);
    } else if (cmd == 'D') {
        int n = vga_esc_params[0];
        if (n == 0) n = 1;
        if (vga_column >= (size_t)n) vga_column -= n;
        else vga_column = 0;
        vga_set_cursor((int)vga_column, (int)vga_row);
    } else if (cmd == 'K') {
        int mode = vga_esc_params[0];
        if (mode == 0 || mode == 2) {
            for (size_t x = (mode == 2 ? 0 : vga_column); x < max_cols; x++) {
                if (is_framebuffer) {
                    vga_draw_rect(x * font_width, vga_row * font_height, font_width, font_height, vga_bg_color);
                } else {
                    text_buffer[vga_row * max_cols + x] = vga_entry(' ', vga_color);
                }
            }
        }
    } else if (cmd == 'm') {
        for (int i = 0; i < vga_esc_pcount; i++) {
            int p = vga_esc_params[i];
            if (p == 0) {
                vga_sgr_reset();
            } else if (p == 7) {
                vga_reverse = true;
                vga_update_color();
            } else if (p == 27) {
                vga_reverse = false;
                vga_update_color();
            } else if (p >= 30 && p <= 37) {
                vga_ansi_fg = p - 30;
                vga_update_color();
            } else if (p >= 40 && p <= 47) {
                vga_ansi_bg = p - 40;
                vga_update_color();
            } else if (p >= 90 && p <= 97) {
                vga_ansi_fg = p - 90 + 8;
                vga_update_color();
            } else if (p >= 100 && p <= 107) {
                vga_ansi_bg = p - 100 + 8;
                vga_update_color();
            } else if (p == 39) {
                vga_ansi_fg = 7;
                vga_update_color();
            } else if (p == 49) {
                vga_ansi_bg = 0;
                vga_update_color();
            }
        }
    }
}

static void vga_handle_esc_char(char c) {
    if (vga_esc_state == 1) {
        if (c == '[') {
            vga_esc_state = 2;
            vga_esc_pcount = 0;
            vga_esc_val = 0;
        } else {
            vga_esc_state = 0;
        }
        return;
    }

    if (vga_esc_state == 3) {
        if (c >= '0' && c <= '9') {
            vga_esc_val = vga_esc_val * 10 + (c - '0');
        } else if (c == 'h' || c == 'l') {
            vga_esc_state = 0;
        } else {
            vga_esc_state = 0;
        }
        return;
    }

    if (vga_esc_state == 2) {
        if (c == '?') {
            vga_esc_state = 3;
            vga_esc_val = 0;
            return;
        }

        if (c >= '0' && c <= '9') {
            vga_esc_val = vga_esc_val * 10 + (c - '0');
            return;
        }

        if (c == ';') {
            if (vga_esc_pcount < 16) {
                vga_esc_params[vga_esc_pcount++] = vga_esc_val;
            }
            vga_esc_val = 0;
            return;
        }

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
            if (vga_esc_pcount < 16) {
                vga_esc_params[vga_esc_pcount++] = vga_esc_val;
            }
            vga_esc_val = c;
            vga_exec_esc();
            vga_esc_state = 0;
            return;
        }

        vga_esc_state = 0;
        return;
    }

    vga_esc_state = 0;
}

void vga_set_cursor(int x, int y) {
    if (is_framebuffer) {
        // No hardware cursor in framebuffer mode for now.
        // We could draw an underscore if we want.
    } else {
        uint16_t pos = y * max_cols + x;
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
}

void vga_set_cursor_pos(int x, int y) {
    vga_column = (size_t)x;
    vga_row = (size_t)y;
    vga_set_cursor(x, y);
}

void vga_show_cursor(int visible) {
    if (!is_framebuffer) {
        if (visible) {
            outb(0x3D4, 0x0A);
            outb(0x3D5, 0x0E);
            outb(0x3D4, 0x0B);
            outb(0x3D5, 0x0F);
        } else {
            outb(0x3D4, 0x0A);
            outb(0x3D5, 0x20);
        }
    }
}

void vga_clear_line(int row) {
    if (row < 0 || (size_t)row >= max_rows) return;
    for (size_t x = 0; x < max_cols; x++) {
        if (is_framebuffer) {
            vga_draw_rect(x * font_width, row * font_height, font_width, font_height, vga_bg_color);
        } else {
            text_buffer[(size_t)row * max_cols + x] = vga_entry(' ', vga_color);
        }
    }
}

void vga_clear_to_eol(void) {
    for (size_t x = vga_column; x < max_cols; x++) {
        if (is_framebuffer) {
            vga_draw_rect(x * font_width, vga_row * font_height, font_width, font_height, vga_bg_color);
        } else {
            text_buffer[vga_row * max_cols + x] = vga_entry(' ', vga_color);
        }
    }
}

void vga_init(void) {
    if (vga_backbuffer) {
        kfree(vga_backbuffer);
        vga_backbuffer = NULL;
    }
    text_buffer = (uint16_t *)phys_to_virt(0xB8000);
    is_framebuffer = false;
    max_cols = 80;
    max_rows = 25;

    /* Se o GRUB reportar 32bpp sem preencher a mascara, define layout ARGB padrao */
    if (vga_fb_bpp == 32 && vga_fb_red_size == 0 && vga_fb_blue_size == 0) {
        vga_fb_red_pos = 16;
        vga_fb_red_size = 8;
        vga_fb_green_pos = 8;
        vga_fb_green_size = 8;
        vga_fb_blue_pos = 0;
        vga_fb_blue_size = 8;
    }

    /* UEFI machines commonly have no usable VGA text memory. Render the
     * console into GRUB's RGB framebuffer when its layout is supported. */
    if (vga_fb_addr && vga_fb_type == 1 && vga_fb_bpp == 32 &&
        vga_fb_red_size == 8 && vga_fb_green_size == 8 &&
        vga_fb_blue_size == 8 && vga_fb_red_pos <= 24 &&
        vga_fb_green_pos <= 24 && vga_fb_blue_pos <= 24 &&
        vga_fb_width >= 80 * 8 &&
        vga_fb_height >= 25 * 16 && vga_fb_pitch >= vga_fb_width * 4U &&
        vga_fb_addr < DIRECT_MAP_SIZE &&
        (uint64_t)vga_fb_pitch * vga_fb_height <=
            DIRECT_MAP_SIZE - vga_fb_addr) {
        uint8_t *font_hdr = _binary_src_drivers_font_psf_start;
        if (font_hdr[0] == 0x36 && font_hdr[1] == 0x04 && font_hdr[3] == 16) {
            font_data = font_hdr + 4;
            font_bpg = font_hdr[3];
            uint64_t fb_size = (uint64_t)vga_fb_pitch * vga_fb_height;
            vmm_map_framebuffer(vga_fb_addr, fb_size);
            is_framebuffer = true;
            max_cols = vga_fb_width / (size_t)font_width;
            max_rows = vga_fb_height / (size_t)font_height;
            vga_backbuffer = (uint32_t *)kmalloc(fb_size);
            if (vga_backbuffer) {
                memset(vga_backbuffer, 0, fb_size);
            }
            serial_print("[console] text framebuffer active\n");
        }
    }

    if (!is_framebuffer) {
        vmm_map_page_in(kernel_directory, (void *)0xB8000,
                        phys_to_virt(0xB8000),
                        PTE_P | PTE_W | PTE_PCD | PTE_NX);
        serial_print("[console] VGA text fallback active\n");
    }
    
    vga_row = 0;
    vga_column = 0;
    vga_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_fg_color = vga_palette[7];
    vga_bg_color = vga_palette[0];
    
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    vga_color = vga_entry_color(fg, bg);
    vga_fg_color = vga_palette[fg];
    vga_bg_color = vga_palette[bg];
}

void vga_putentryat(char c, uint8_t color, size_t x, size_t y) {
    if (is_framebuffer) {
        vga_draw_char(c, x * font_width, y * font_height, vga_fg_color, vga_bg_color);
    } else {
        const size_t index = y * max_cols + x;
        text_buffer[index] = vga_entry(c, color);
    }
}

void vga_scroll() {
    if (is_framebuffer) {
        uint32_t stride_words = vga_fb_pitch / 4;
        uint32_t row_words = stride_words * (uint32_t)font_height;
        uint32_t total_scroll_words =
            stride_words * (uint32_t)((max_rows - 1) * font_height);
        uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);

        if (vga_backbuffer) {
            memmove(vga_backbuffer, vga_backbuffer + row_words,
                    total_scroll_words * sizeof(uint32_t));
            uint32_t bg_px = vga_convert_color(vga_bg_color);
            uint32_t *bottom = vga_backbuffer + total_scroll_words;
            for (uint32_t i = 0; i < row_words; i++) {
                bottom[i] = bg_px;
            }
            fast_memcpy(fb, vga_backbuffer,
                        (total_scroll_words + row_words) * sizeof(uint32_t));
        } else {
            memmove(fb, fb + row_words, total_scroll_words * 4);
            vga_draw_rect(0, (max_rows - 1) * font_height, vga_fb_width, font_height, vga_bg_color);
        }
    } else {
        for (size_t y = 0; y < max_rows - 1; y++) {
            for (size_t x = 0; x < max_cols; x++) {
                text_buffer[y * max_cols + x] = text_buffer[(y + 1) * max_cols + x];
            }
        }
        for (size_t x = 0; x < max_cols; x++) {
            text_buffer[(max_rows - 1) * max_cols + x] = vga_entry(' ', vga_color);
        }
    }
}

static inline uint32_t vga_pack_xrgb(uint32_t xrgb) {
    if (vga_fb_red_pos == 16 && vga_fb_green_pos == 8 && vga_fb_blue_pos == 0) {
        return xrgb & 0x00FFFFFFU;
    }
    uint32_t r = (xrgb >> 16) & 0xFFU;
    uint32_t g = (xrgb >> 8) & 0xFFU;
    uint32_t b = xrgb & 0xFFU;
    return (r << vga_fb_red_pos) | (g << vga_fb_green_pos) | (b << vga_fb_blue_pos);
}

static inline void vga_copy_row(void *dst, const void *src, size_t nbytes) {
    uint64_t *d64 = (uint64_t *)dst;
    const uint64_t *s64 = (const uint64_t *)src;
    size_t q = nbytes >> 3;
    while (q--) *d64++ = *s64++;
    uint8_t *d8 = (uint8_t *)d64;
    const uint8_t *s8 = (const uint8_t *)s64;
    size_t r = nbytes & 7U;
    while (r--) *d8++ = *s8++;
}

void vga_clear_framebuffer(uint32_t xrgb) {
    if (!vga_fb_addr || vga_fb_bpp != 32 || !vga_fb_width || !vga_fb_height) return;
    uint32_t px = vga_pack_xrgb(xrgb);
    uint32_t stride_words = vga_fb_pitch / 4U;
    uint32_t *fb = (uint32_t *)phys_to_virt(vga_fb_addr);
    for (uint32_t y = 0; y < vga_fb_height; y++) {
        uint32_t *dst_row = vga_backbuffer ? (vga_backbuffer + y * stride_words)
                                           : (fb + y * stride_words);
        for (uint32_t x = 0; x < vga_fb_width; x++) {
            dst_row[x] = px;
        }
        if (vga_backbuffer) {
            vga_copy_row(fb + y * stride_words, dst_row, (size_t)vga_fb_width * 4U);
        }
    }
}

int vga_blit_rgb32(const uint32_t *src, uint32_t src_w, uint32_t src_h) {
    if (!src || src_w == 0 || src_h == 0) return -1;
    if (!vga_fb_addr || vga_fb_bpp != 32 || !vga_fb_width || !vga_fb_height) return -1;

    uint32_t sx = vga_fb_width / src_w;
    uint32_t sy = vga_fb_height / src_h;
    uint32_t scale = (sx < sy) ? sx : sy;
    uint32_t dst_w = (scale >= 1U) ? (src_w * scale) : vga_fb_width;
    uint32_t dst_h = (scale >= 1U) ? (src_h * scale) : vga_fb_height;
    uint32_t x0 = (vga_fb_width - dst_w) / 2U;
    uint32_t y0 = (vga_fb_height - dst_h) / 2U;
    uint32_t stride_words = vga_fb_pitch / 4U;
    uint32_t *fb = (uint32_t *)phys_to_virt(vga_fb_addr);
    bool native_xrgb = (vga_fb_red_pos == 16 && vga_fb_green_pos == 8 && vga_fb_blue_pos == 0);

    if (scale >= 1U && vga_backbuffer) {
        size_t row_bytes = (size_t)dst_w * 4U;
        for (uint32_t src_y = 0; src_y < src_h; src_y++) {
            const uint32_t *srow = src + src_y * src_w;
            uint32_t base_dy = y0 + src_y * scale;
            uint32_t *first_bb = vga_backbuffer + base_dy * stride_words + x0;

            if (scale == 1U && native_xrgb) {
                vga_copy_row(first_bb, srow, row_bytes);
            } else {
                uint32_t *out = first_bb;
                for (uint32_t src_x = 0; src_x < src_w; src_x++) {
                    uint32_t px = native_xrgb ? (srow[src_x] & 0x00FFFFFFU) : vga_pack_xrgb(srow[src_x]);
                    for (uint32_t k = 0; k < scale; k++) {
                        *out++ = px;
                    }
                }
            }

            vga_copy_row(fb + base_dy * stride_words + x0, first_bb, row_bytes);
            for (uint32_t rep = 1U; rep < scale; rep++) {
                uint32_t dy = base_dy + rep;
                uint32_t *bb_row = vga_backbuffer + dy * stride_words + x0;
                vga_copy_row(bb_row, first_bb, row_bytes);
                vga_copy_row(fb + dy * stride_words + x0, first_bb, row_bytes);
            }
        }
        return 0;
    }

    for (uint32_t dy = 0; dy < dst_h; dy++) {
        uint32_t sy_idx = (dy * src_h) / dst_h;
        const uint32_t *srow = src + sy_idx * src_w;
        uint32_t *out = (vga_backbuffer ? vga_backbuffer : fb) + (y0 + dy) * stride_words + x0;
        for (uint32_t dx = 0; dx < dst_w; dx++) {
            uint32_t sx_idx = (dx * src_w) / dst_w;
            out[dx] = native_xrgb ? (srow[sx_idx] & 0x00FFFFFFU) : vga_pack_xrgb(srow[sx_idx]);
        }
        if (vga_backbuffer) {
            vga_copy_row(fb + (y0 + dy) * stride_words + x0, out, (size_t)dst_w * 4U);
        }
    }
    return 0;
}

void (*vga_puts_capture_hook)(const char *str) = NULL;
void (*vga_putc_capture_hook)(char c) = NULL;
extern bool g_desktop_running;

void vga_putc(char c) {
    if (vga_putc_capture_hook) {
        vga_putc_capture_hook(c);
        return;
    }
    if (g_desktop_running) {
        /* Desktop gráfico está ativo e sem janela capturando:
         * JAMAIS desenhe pixels brutos de texto sobre o LVGL! */
        return;
    }
    if (vga_esc_state > 0) {
        vga_handle_esc_char(c);
        return;
    }
    if (c == '\x1b') {
        vga_esc_state = 1;
        vga_esc_pcount = 0;
        vga_esc_val = 0;
        return;
    }

    if (c == '\n') {
        vga_column = 0;
        if (++vga_row >= max_rows) {
            vga_scroll();
            vga_row = max_rows - 1;
        }
    } else if (c == '\r') {
        vga_column = 0;
    } else if (c == '\t') {
        vga_column += 4;
        if (vga_column >= max_cols) {
            vga_column = 0;
            if (++vga_row >= max_rows) {
                vga_scroll();
                vga_row = max_rows - 1;
            }
        }
    } else if (c == '\b') {
        if (vga_column > 0) {
            vga_column--;
            vga_putentryat(' ', vga_color, vga_column, vga_row);
        } else if (vga_row > 0) {
            vga_row--;
            vga_column = max_cols - 1;
            vga_putentryat(' ', vga_color, vga_column, vga_row);
        }
    } else {
        vga_putentryat(c, vga_color, vga_column, vga_row);
        if (++vga_column >= max_cols) {
            vga_column = 0;
            if (++vga_row >= max_rows) {
                vga_scroll();
                vga_row = max_rows - 1;
            }
        }
    }
    vga_set_cursor((int)vga_column, (int)vga_row);
}

void vga_puts(const char* str) {
    if (vga_puts_capture_hook) {
        vga_puts_capture_hook(str);
        return;
    }
    if (g_desktop_running) {
        return;
    }
    for (size_t i = 0; str[i] != '\0'; i++)
        vga_putc(str[i]);
}

void vga_clear(enum vga_color fg, enum vga_color bg) {
    vga_color = vga_entry_color(fg, bg);
    vga_fg_color = vga_palette[fg];
    vga_bg_color = vga_palette[bg];
    
    if (is_framebuffer) {
        vga_draw_rect(0, 0, vga_fb_width, vga_fb_height, vga_bg_color);
    } else {
        for (size_t y = 0; y < max_rows; y++) {
            for (size_t x = 0; x < max_cols; x++) {
                text_buffer[y * max_cols + x] = vga_entry(' ', vga_color);
            }
        }
    }
    vga_row = 0;
    vga_column = 0;
    vga_set_cursor(0, 0);
}

void vga_get_dimensions(size_t *cols, size_t *rows) {
    if (cols) *cols = max_cols;
    if (rows) *rows = max_rows;
}

bool vga_is_framebuffer(void) {
    return is_framebuffer;
}

uint32_t *vga_get_backbuffer(void) {
    return vga_backbuffer;
}

uint32_t vga_get_fb_width(void) {
    return vga_fb_width;
}

uint32_t vga_get_fb_height(void) {
    return vga_fb_height;
}

uint32_t vga_get_fb_pitch(void) {
    return vga_fb_pitch;
}

void vga_draw_char_fb(char c, int x, int y, uint32_t fg, uint32_t bg) {
    if (!is_framebuffer || !font_data || !vga_backbuffer) return;
    if (x < 0 || y < 0) return;
    if ((uint32_t)(x + 8) > vga_fb_width || (uint32_t)(y + 16) > vga_fb_height) return;

    uint8_t* glyph = font_data + (unsigned char)c * font_bpg;
    uint32_t fg_px = vga_convert_color(fg);
    uint32_t bg_px = (bg != 0xFFFFFFFF) ? vga_convert_color(bg) : 0;
    uint32_t stride_words = vga_fb_pitch / 4;
    uint32_t* back_dst = vga_backbuffer + ((uint32_t)y * stride_words) + (uint32_t)x;

    for (int j = 0; j < 16; j++) {
        uint8_t row = glyph[j];
        for (int b = 0; b < 8; b++) {
            if (row & (0x80 >> b)) {
                back_dst[b] = fg_px;
            } else if (bg != 0xFFFFFFFF) {
                back_dst[b] = bg_px;
            }
        }
        back_dst += stride_words;
    }
}

void vga_flip(void) {
    if (!is_framebuffer || !vga_backbuffer) return;
    uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);
    uint64_t bytes = (uint64_t)vga_fb_pitch * vga_fb_height;
    fast_memcpy(fb, vga_backbuffer, bytes);
}

static void vga_bmp_put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void vga_bmp_put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

int vga_capture_bmp(uint8_t **out_data, uint32_t *out_size) {
    if (!out_data || !out_size) return -1;
    *out_data = NULL;
    *out_size = 0;
    if (!is_framebuffer || vga_fb_bpp != 32 || !vga_fb_width || !vga_fb_height ||
        !vga_fb_addr ||
        (uint64_t)vga_fb_pitch < (uint64_t)vga_fb_width * sizeof(uint32_t) ||
        vga_fb_red_pos > 24 || vga_fb_green_pos > 24 || vga_fb_blue_pos > 24)
        return -2;

    uint64_t row_bytes = (uint64_t)vga_fb_width * 3;
    uint64_t bmp_stride = (row_bytes + 3) & ~3ULL;
    uint64_t pixel_bytes = bmp_stride * vga_fb_height;
    uint64_t total_bytes = 54 + pixel_bytes;
    if (vga_fb_width > 8192 || vga_fb_height > 8192 ||
        total_bytes > 32ULL * 1024 * 1024 || total_bytes > UINT32_MAX)
        return -3;

    uint8_t *bmp = (uint8_t *)kmalloc((size_t)total_bytes);
    if (!bmp) return -4;
    memset(bmp, 0, (size_t)total_bytes);

    bmp[0] = 'B'; bmp[1] = 'M';
    vga_bmp_put32(bmp + 2, (uint32_t)total_bytes);
    vga_bmp_put32(bmp + 10, 54);
    vga_bmp_put32(bmp + 14, 40);
    vga_bmp_put32(bmp + 18, vga_fb_width);
    vga_bmp_put32(bmp + 22, vga_fb_height); /* Positive height: bottom-up BMP. */
    vga_bmp_put16(bmp + 26, 1);
    vga_bmp_put16(bmp + 28, 24);
    vga_bmp_put32(bmp + 34, (uint32_t)pixel_bytes);
    vga_bmp_put32(bmp + 38, 2835);
    vga_bmp_put32(bmp + 42, 2835);

    /* Read the scanout itself so the image matches what is currently visible,
     * including renderers that draw directly instead of updating the backbuffer. */
    const uint32_t *source = (const uint32_t *)phys_to_virt(vga_fb_addr);
    uint32_t source_stride = vga_fb_pitch / sizeof(uint32_t);
    uint32_t output_stride = (uint32_t)bmp_stride;
    for (uint32_t y = 0; y < vga_fb_height; y++) {
        const uint32_t *src = source + (size_t)(vga_fb_height - 1 - y) * source_stride;
        uint8_t *dst = bmp + 54 + (size_t)y * output_stride;
        for (uint32_t x = 0; x < vga_fb_width; x++) {
            uint32_t pixel = src[x];
            dst[x * 3 + 0] = (uint8_t)(pixel >> vga_fb_blue_pos);
            dst[x * 3 + 1] = (uint8_t)(pixel >> vga_fb_green_pos);
            dst[x * 3 + 2] = (uint8_t)(pixel >> vga_fb_red_pos);
        }
    }

    *out_data = bmp;
    *out_size = (uint32_t)total_bytes;
    return 0;
}

