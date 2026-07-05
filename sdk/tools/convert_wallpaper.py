import sys
from PIL import Image

def convert_wallpaper(input_path, output_path):
    try:
        img = Image.open(input_path).convert('RGB')
        img = img.resize((1024, 768), Image.Resampling.LANCZOS)
        
        with open(output_path, 'w') as f:
            f.write("// Auto-generated wallpaper (Asset Pipeline)\n")
            f.write("#ifndef WALLPAPER_H\n")
            f.write("#define WALLPAPER_H\n\n")
            f.write("#include <stdint.h>\n\n")
            f.write("static const int wallpaper_width = 1024;\n")
            f.write("static const int wallpaper_height = 768;\n\n")
            
            f.write("static const uint32_t wallpaper_data[1024 * 768] = {\n")
            
            pixels = list(img.getdata())
            for i, p in enumerate(pixels):
                r, g, b = p
                # ARGB (Alpha is 0xFF)
                val = (0xFF << 24) | (r << 16) | (g << 8) | b
                f.write(f"0x{val:08X}")
                if i < len(pixels) - 1:
                    f.write(",")
                if (i + 1) % 16 == 0:
                    f.write("\n")
                    
            f.write("};\n\n")
            f.write("#endif\n")
        print(f"Wallpaper generated at {output_path}")
    except Exception as e:
        print(f"Failed to generate wallpaper: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 convert_wallpaper.py <input> <output>")
    else:
        convert_wallpaper(sys.argv[1], sys.argv[2])
