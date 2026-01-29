#!/usr/bin/env python3
"""
Generate a green phosphor CRT pixel art wallpaper for LiwusOS.
Output: wallpaper_green.h (C header with uint32_t wallpaper_data[])
"""
import os

W, H = 1024, 768

def pixel(r, g, b, a=0xFF):
    return (a << 24) | (r << 16) | (g << 8) | b

# Color palette - green phosphor CRT
BLACK = pixel(10, 10, 18)       # Near-black with faint blue
DARK_GREEN = pixel(0, 20, 10)   # Very dark green
GRID_GREEN = pixel(0, 40, 20)   # Grid dots
DIM_GREEN = pixel(0, 60, 30)    # Dim green elements
MID_GREEN = pixel(0, 120, 60)   # Medium green
BRIGHT_GREEN = pixel(0, 255, 65) # Bright phosphor green

def generate_wallpaper():
    pixels = [[BLACK for _ in range(W)] for _ in range(H)]

    # 1. Subtle grid pattern (every 8 pixels, like CRT pixel grid)
    for y in range(0, H, 8):
        for x in range(0, W, 8):
            pixels[y][x] = GRID_GREEN

    # 2. Larger grid pattern (every 32 pixels, like monitor dots)
    for y in range(0, H, 32):
        for x in range(0, W, 32):
            pixels[y][x] = DIM_GREEN

    # 3. Draw a pixel art CRT monitor in the center
    # Monitor body (outer frame)
    mx, my = 380, 240  # top-left corner
    # Monitor shell
    for y in range(my, my + 200):
        for x in range(mx, mx + 264):
            pixels[y][x] = DARK_GREEN
    # Monitor border (bright)
    for x in range(mx, mx + 264):
        pixels[my][x] = BRIGHT_GREEN
        pixels[my + 199][x] = BRIGHT_GREEN
    for y in range(my, my + 200):
        pixels[y][mx] = BRIGHT_GREEN
        pixels[y][mx + 263] = BRIGHT_GREEN

    # Screen area (inside the monitor)
    sx, sy = mx + 16, my + 16
    sw, sh = 232, 152
    for y in range(sy, sy + sh):
        for x in range(sx, sx + sw):
            pixels[y][x] = pixel(0, 8, 4)  # Very dark green screen

    # Screen border
    for x in range(sx, sx + sw):
        pixels[sy][x] = MID_GREEN
        pixels[sy + sh - 1][x] = MID_GREEN
    for y in range(sy, sy + sh):
        pixels[y][sx] = MID_GREEN
        pixels[y][sx + sw - 1] = MID_GREEN

    # Text on screen: "LIWUS" in pixel font (5x7 font, scaled 3x)
    def draw_char_5x7(cx, cy, char, color):
        font = {
            'L': [0x7F, 0x40, 0x40, 0x40, 0x40],
            'I': [0x41, 0x41, 0x7F, 0x41, 0x41],
            'W': [0x3F, 0x40, 0x38, 0x40, 0x3F],
            'U': [0x1F, 0x20, 0x20, 0x20, 0x1F],
            'S': [0x27, 0x49, 0x49, 0x49, 0x31],
            'O': [0x3E, 0x41, 0x41, 0x41, 0x3E],
            ' ': [0x00, 0x00, 0x00, 0x00, 0x00],
        }
        if char not in font:
            return
        glyph = font[char]
        for col in range(5):
            bits = glyph[col]
            for row in range(7):
                if bits & (1 << row):
                    for dy in range(3):
                        for dx in range(3):
                            pixels[cy + row * 3 + dy][cx + col * 3 + dx] = color

    # "LIWUS OS" text centered on screen
    text = "LIWUS"
    char_w = 5 * 3  # 15px per char
    spacing = 3 * 3  # 9px spacing
    total_w = len(text) * char_w + (len(text) - 1) * spacing
    text_x = sx + (sw - total_w) // 2
    text_y = sy + 30
    draw_char_5x7(text_x, text_y, 'L', BRIGHT_GREEN)
    draw_char_5x7(text_x + 1 * (char_w + spacing), text_y, 'I', BRIGHT_GREEN)
    draw_char_5x7(text_x + 2 * (char_w + spacing), text_y, 'W', BRIGHT_GREEN)
    draw_char_5x7(text_x + 3 * (char_w + spacing), text_y, 'U', BRIGHT_GREEN)
    draw_char_5x7(text_x + 4 * (char_w + spacing), text_y, 'S', BRIGHT_GREEN)

    # "OS" below
    os_text = "OS"
    os_total_w = 2 * char_w + spacing
    os_x = sx + (sw - os_total_w) // 2
    os_y = text_y + 7 * 3 + 12
    draw_char_5x7(os_x, os_y, 'O', MID_GREEN)
    draw_char_5x7(os_x + char_w + spacing, os_y, 'S', MID_GREEN)

    # Blinking cursor on screen
    cursor_x = sx + 40
    cursor_y = sy + sh - 30
    for dy in range(12):
        for dx in range(8):
            pixels[cursor_y + dy][cursor_x + dx] = BRIGHT_GREEN

    # Monitor stand
    stand_cx = mx + 132  # center
    for y in range(my + 200, my + 230):
        for x in range(stand_cx - 20, stand_cx + 20):
            if y < my + 230:
                pixels[y][x] = DARK_GREEN
    # Stand base
    for y in range(my + 228, my + 236):
        for x in range(stand_cx - 40, stand_cx + 40):
            pixels[y][x] = GRID_GREEN

    # 4. Add some floating "data" particles around the monitor
    import random
    random.seed(42)  # Deterministic
    for _ in range(200):
        x = random.randint(0, W - 1)
        y = random.randint(0, H - 1)
        # Skip if inside the monitor area
        if mx <= x <= mx + 264 and my <= y <= my + 236:
            continue
        brightness = random.choice([GRID_GREEN, DIM_GREEN, MID_GREEN])
        pixels[y][x] = brightness
        # Small clusters
        for _ in range(random.randint(1, 3)):
            nx = x + random.randint(-2, 2)
            ny = y + random.randint(-2, 2)
            if 0 <= nx < W and 0 <= ny < H:
                if not (mx <= nx <= mx + 264 and my <= ny <= my + 236):
                    pixels[ny][nx] = brightness

    # 5. Horizontal scan line effect (subtle bright lines every 128 pixels)
    for y in range(0, H, 128):
        for x in range(W):
            r = (pixels[y][x] >> 16) & 0xFF
            g = (pixels[y][x] >> 8) & 0xFF
            b = pixels[y][x] & 0xFF
            g = min(255, g + 15)
            pixels[y][x] = pixel(r, g, b)

    return pixels

def write_header(pixels, output_path):
    with open(output_path, 'w') as f:
        f.write('// Auto-generated CRT Green Phosphor wallpaper\n')
        f.write('#ifndef WALLPAPER_H\n')
        f.write('#define WALLPAPER_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'static const int wallpaper_width = {W};\n')
        f.write(f'static const int wallpaper_height = {H};\n\n')
        f.write(f'static const uint32_t wallpaper_data[{W} * {H}] = {{\n')

        for y in range(H):
            row_vals = []
            for x in range(W):
                p = pixels[y][x]
                row_vals.append(f'0x{p:08X}')
            f.write(','.join(row_vals))
            if y < H - 1:
                f.write(',')
            f.write('\n')

        f.write('};\n\n')
        f.write('#endif /* WALLPAPER_H */\n')

if __name__ == '__main__':
    print('Generating CRT green phosphor wallpaper (1024x768)...')
    pixels = generate_wallpaper()
    out = os.path.join(os.path.dirname(__file__), '..', 'include', 'gui', 'wallpaper.h')
    write_header(pixels, out)
    print(f'Done! Written to {out}')
    print(f'File size: {os.path.getsize(out) / 1024 / 1024:.1f} MB')
