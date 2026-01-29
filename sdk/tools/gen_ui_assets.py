import os
from PIL import Image, ImageDraw, ImageFilter

def generate_button(color, outline, name):
    size = 12
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Anti-aliased circle
    # draw a larger circle and resize for anti-aliasing
    scale = 4
    large_img = Image.new('RGBA', (size*scale, size*scale), (0, 0, 0, 0))
    large_draw = ImageDraw.Draw(large_img)
    large_draw.ellipse([0, 0, size*scale-1, size*scale-1], fill=color, outline=outline, width=scale)
    img = large_img.resize((size, size), Image.Resampling.LANCZOS)
    return img

def img_to_c_array(img, array_name):
    width, height = img.size
    pixels = img.load()
    c_str = f"const int {array_name}_width = {width};\n"
    c_str += f"const int {array_name}_height = {height};\n"
    c_str += f"const uint32_t {array_name}_data[] = {{\n"
    for y in range(height):
        c_str += "    "
        for x in range(width):
            r, g, b, a = pixels[x, y]
            # Convert to ARGB format
            val = (a << 24) | (r << 16) | (g << 8) | b
            c_str += f"0x{val:08X}, "
        c_str += "\n"
    c_str += "};\n"
    return c_str

def main():
    assets_h = "#ifndef UI_ASSETS_H\n#define UI_ASSETS_H\n\n#include <stdint.h>\n\n"

    # 1. macOS-style Buttons
    btn_close = generate_button((255, 95, 86, 255), (224, 68, 62, 255), "btn_close")
    btn_min = generate_button((255, 189, 46, 255), (222, 161, 35, 255), "btn_min")
    btn_max = generate_button((39, 201, 63, 255), (26, 171, 41, 255), "btn_max")

    assets_h += img_to_c_array(btn_close, "ui_btn_close")
    assets_h += img_to_c_array(btn_min, "ui_btn_min")
    assets_h += img_to_c_array(btn_max, "ui_btn_max")

    # 2. Window Shadow and Border (9-slice corners)
    # To keep it simple, we will generate a rounded corner bitmap (radius = 8)
    # The compositor will draw this at the corners, and straight lines for edges.
    r = 8
    # Shadow Generation
    SHADOW_OFFSET = 10
    SHADOW_BLUR = 10
    SHADOW_COLOR = (0, 0, 0, 80)
    scale = 4
    large_corner = Image.new('RGBA', (r*scale*2, r*scale*2), (0, 0, 0, 0))
    large_draw = ImageDraw.Draw(large_corner)
    # Draw a filled circle and we take the top-left quadrant for the top-left corner
    # The background color of the window title bar is Slate 800 (30, 41, 59)
    # We'll make the corner translucent at the edges
    large_draw.ellipse([0, 0, r*2*scale-1, r*2*scale-1], fill=(30, 41, 59, 255))
    corner_img = large_corner.resize((r*2, r*2), Image.Resampling.LANCZOS)
    
    # Crop top-left corner
    tl_corner = corner_img.crop((0, 0, r, r))
    assets_h += img_to_c_array(tl_corner, "ui_tl_corner")

    # Crop top-right corner
    tr_corner = corner_img.crop((r, 0, r*2, r))
    assets_h += img_to_c_array(tr_corner, "ui_tr_corner")

    # 3. Drop Shadow Slices
    # We create a 64x64 white rounded rect, blur it, and crop the corners and edges.
    shadow_r = 6
    shadow_blur = 3
    shadow_size = 64
    s_img = Image.new('RGBA', (shadow_size, shadow_size), (0, 0, 0, 0))
    s_draw = ImageDraw.Draw(s_img)
    s_draw.rounded_rectangle([shadow_blur, shadow_blur, shadow_size-shadow_blur-1, shadow_size-shadow_blur-1], radius=shadow_r, fill=(0, 0, 0, 80))
    s_img = s_img.filter(ImageFilter.GaussianBlur(radius=shadow_blur))

    # The corners will be 12x12 pixels to capture the blur and corner
    c_size = 12
    assets_h += img_to_c_array(s_img.crop((0, 0, c_size, c_size)), "ui_shadow_tl")
    assets_h += img_to_c_array(s_img.crop((shadow_size-c_size, 0, shadow_size, c_size)), "ui_shadow_tr")
    assets_h += img_to_c_array(s_img.crop((0, shadow_size-c_size, c_size, shadow_size)), "ui_shadow_bl")
    assets_h += img_to_c_array(s_img.crop((shadow_size-c_size, shadow_size-c_size, shadow_size, shadow_size)), "ui_shadow_br")

    # Edges are 1x24 or 24x1 pixels (taken from the middle)
    assets_h += img_to_c_array(s_img.crop((shadow_size//2, 0, shadow_size//2 + 1, c_size)), "ui_shadow_top")
    assets_h += img_to_c_array(s_img.crop((shadow_size//2, shadow_size-c_size, shadow_size//2 + 1, shadow_size)), "ui_shadow_bottom")
    assets_h += img_to_c_array(s_img.crop((0, shadow_size//2, c_size, shadow_size//2 + 1)), "ui_shadow_left")
    assets_h += img_to_c_array(s_img.crop((shadow_size-c_size, shadow_size//2, shadow_size, shadow_size//2 + 1)), "ui_shadow_right")

    # 4. Radial Menu (Pie Menu) base
    pie_size = 120
    pie_img = Image.new('RGBA', (pie_size, pie_size), (0, 0, 0, 0))
    pie_draw = ImageDraw.Draw(pie_img)
    # Fundo circular translúcido (Slate 900 com 60% opacidade)
    pie_draw.ellipse([0, 0, pie_size-1, pie_size-1], fill=(15, 23, 42, 180))
    # Borda circular mais clara
    pie_draw.ellipse([0, 0, pie_size-1, pie_size-1], outline=(148, 163, 184, 100), width=2)
    assets_h += img_to_c_array(pie_img, "ui_pie_menu")

    # Pie Menu Button (Terminal)
    btn_size = 32
    btn_img = Image.new('RGBA', (btn_size, btn_size), (0, 0, 0, 0))
    btn_draw = ImageDraw.Draw(btn_img)
    btn_draw.ellipse([0, 0, btn_size-1, btn_size-1], fill=(59, 130, 246, 255)) # Blue 500
    assets_h += img_to_c_array(btn_img, "ui_pie_btn_app")

    # Pie Menu Button (Close)
    btn_close_img = Image.new('RGBA', (btn_size, btn_size), (0, 0, 0, 0))
    btn_close_draw = ImageDraw.Draw(btn_close_img)
    btn_close_draw.ellipse([0, 0, btn_size-1, btn_size-1], fill=(239, 68, 68, 255)) # Red 500
    assets_h += img_to_c_array(btn_close_img, "ui_pie_btn_close")

    # 5. App Launcher (Dock)
    dock_w = 120 # Smaller dock for just 1 app
    dock_h = 80
    dock_ai_path = r"/mnt/c/Users/ranci/.gemini/antigravity/brain/afae991b-1cbe-41c9-99f8-04dd11c3e021/dock_bg_ai_v2_1783274223079.jpg"
    try:
        dock_img = Image.open(dock_ai_path).convert('RGBA').resize((dock_w, dock_h), Image.Resampling.LANCZOS)
    except Exception:
        # Fallback if image not found during dev
        dock_img = Image.new('RGBA', (dock_w, dock_h), (0, 0, 0, 0))
        ImageDraw.Draw(dock_img).rounded_rectangle([0, 0, dock_w-1, dock_h-1], radius=16, fill=(15, 23, 42, 220), outline=(148, 163, 184, 150), width=2)
    assets_h += img_to_c_array(dock_img, "ui_app_menu_bg")

    # App Icons (48x48)
    icon_size = 48
    
    # Demo App Logo (AI)
    icon_ai_path = r"/mnt/c/Users/ranci/.gemini/antigravity/brain/afae991b-1cbe-41c9-99f8-04dd11c3e021/demo_icon_ai_v2_1783274217448.jpg"
    try:
        ico_demo = Image.open(icon_ai_path).convert('RGBA').resize((icon_size, icon_size), Image.Resampling.LANCZOS)
        # Apply rounded corners mask to the generated image
        mask = Image.new('L', (icon_size, icon_size), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, icon_size-1, icon_size-1], radius=10, fill=255)
        ico_demo.putalpha(mask)
    except Exception:
        ico_demo = Image.new('RGBA', (icon_size, icon_size), (0, 0, 0, 0))
        ImageDraw.Draw(ico_demo).rounded_rectangle([0, 0, icon_size-1, icon_size-1], radius=10, fill=(71, 85, 105, 255))
    assets_h += img_to_c_array(ico_demo, "ui_icon_demo")

    assets_h += "\n#endif\n"

    # Write to ui_assets.h
    output_path = os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'kernel', 'ui_assets.h')
    with open(output_path, 'w') as f:
        f.write(assets_h)
    print("UI Assets generated at", output_path)

if __name__ == "__main__":
    main()
