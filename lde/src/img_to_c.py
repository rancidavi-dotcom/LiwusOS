import sys
from PIL import Image

def generate_header(img_path, header_path, size=(64, 64)):
    img = Image.open(img_path).convert('RGBA')
    img = img.resize(size, Image.Resampling.LANCZOS)
    
    with open(header_path, 'w') as f:
        f.write("#pragma once\n")
        f.write("#include <stdint.h>\n")
        f.write(f"const int tile_width = {size[0]};\n")
        f.write(f"const int tile_height = {size[1]};\n")
        f.write("const uint32_t tile_data[] = {\n")
        
        for y in range(size[1]):
            line = []
            for x in range(size[0]):
                r, g, b, a = img.getpixel((x, y))
                # For alpha blending later, if white background, maybe we can make it transparent
                # But it's an isometric tile, let's keep it opaque or apply a simple chroma key if white.
                if r > 240 and g > 240 and b > 240:
                    a = 0
                val = (a << 24) | (r << 16) | (g << 8) | b
                line.append(f"0x{val:08X}")
            f.write("    " + ", ".join(line) + ",\n")
            
        f.write("};\n")

if __name__ == '__main__':
    generate_header(sys.argv[1], sys.argv[2])
