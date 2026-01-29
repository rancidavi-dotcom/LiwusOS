import sys

width = 1024
height = 768

print('Generating wallpaper.h...')
with open('include/wallpaper.h', 'w') as f:
    f.write('// Auto-generated wallpaper (Asset Pipeline)\n')
    f.write('#ifndef WALLPAPER_H\n')
    f.write('#define WALLPAPER_H\n\n')
    f.write('#include <stdint.h>\n\n')
    f.write(f'static const int wallpaper_width = {width};\n')
    f.write(f'static const int wallpaper_height = {height};\n\n')
    f.write(f'static const uint32_t wallpaper_data[{width * height}] = {{\n    ')
    
    for y in range(height):
        t = (y * 256) // height
        r1, g1, b1 = 0x0F, 0x17, 0x2A
        r2, g2, b2 = 0x31, 0x2E, 0x81
        
        r = r1 + (t * (r2 - r1)) // 256
        g = g1 + (t * (g2 - g1)) // 256
        b = b1 + (t * (b2 - b1)) // 256
        color = (0xFF << 24) | (r << 16) | (g << 8) | b
        
        for x in range(width):
            f.write(f'0x{color:08X},')
        f.write('\n    ')
        
    f.write('\n};\n\n')
    f.write('#endif // WALLPAPER_H\n')
print('Done!')
