#!/usr/bin/env python3
"""Convert Chicago95 cursors + UI control pixel art into C ARGB headers for
LiwusOS. Generates:
  - src/kernel/gui/assets/pixel_cursors.h
  - src/kernel/gui/assets/pixel_controls.h

Sources (https://github.com/grassmunk/Chicago95, CC BY-SA 4.0):
  Cursors/<set>/build/{95,xcursors}/*.cur   (32x32, hotspots read from file)
  Theme/Chicago95/gtk-3.0/{assets,buttons,scrollbar}/*.png
"""

import os
import struct
import sys
import zlib

THEME = "/mnt/c/Users/davivbr/AppData/Local/Temp/opencode/Chicago95"
OUT_DIR = "/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/src/kernel/gui/assets"

# gui_cursor_t order in compositor.h (must stay in sync):
#   ARROW, HAND, IBEAM, RESIZE_NS, RESIZE_EW, NO, WAIT, RESIZE_NWSE,
#   RESIZE_NESW, MOVE
CURSOR_ORDER = [
    "arrow", "hand", "ibeam", "sizens", "sizewe", "no", "wait",
    "sizenwse", "sizenesw", "sizeall",
]
CURSOR_SRC = {
    "arrow":     "build/95/arrow.cur",
    "hand":      "build/xcursors/hand1.cur",
    "ibeam":     "build/95/ibeam.cur",
    "sizens":    "build/95/sizens.cur",
    "sizewe":    "build/95/sizewe.cur",
    "no":        "build/95/no.cur",
    "wait":      "build/95/wait.cur",
    "sizenwse":  "build/95/sizenwse.cur",
    "sizenesw":  "build/95/sizenesw.cur",
    "sizeall":   "build/95/sizeall.cur",
}

CONTROL_ORDER = [
    "checkbox_unchecked", "checkbox_checked", "checkbox_mixed",
    "radio_unselected", "radio_selected", "radio_mixed",
    "win_close", "win_close_pressed",
    "win_min", "win_min_pressed",
    "win_max", "win_max_pressed",
    "win_restore", "win_restore_pressed",
    "win_btn_normal", "win_btn_pressed",
    "scroll_btn", 
    "stepper_up", "stepper_down", "stepper_left", "stepper_right",
    "stepper_up_active", "stepper_down_active",
    "stepper_left_active", "stepper_right_active",
    "combobox_btn", "combobox_arrow",
    "expander_plus", "expander_minus",
    "arrow_right", "arrow_left", "arrow_up", "arrow_down",
    "handle_h", "handle_v",
    "folder", "file",
]
CONTROL_SRC = {
    "checkbox_unchecked": "gtk-3.0/assets/checkbox-unchecked.png",
    "checkbox_checked":   "gtk-3.0/assets/checkbox-checked.png",
    "checkbox_mixed":     "gtk-3.0/assets/checkbox-mixed.png",
    "radio_unselected":   "gtk-3.0/assets/radio-unselected.png",
    "radio_selected":     "gtk-3.0/assets/radio-selected.png",
    "radio_mixed":        "gtk-3.0/assets/radio-mixed.png",
    "win_close":          "gtk-3.0/buttons/close_normal.png",
    "win_close_pressed":  "gtk-3.0/buttons/close_pressed.png",
    "win_min":            "gtk-3.0/buttons/minimize_normal.png",
    "win_min_pressed":    "gtk-3.0/buttons/minimize_pressed.png",
    "win_max":            "gtk-3.0/buttons/maximize_normal.png",
    "win_max_pressed":    "gtk-3.0/buttons/maximize_pressed.png",
    "win_restore":        "gtk-3.0/buttons/restore_normal.png",
    "win_restore_pressed":"gtk-3.0/buttons/restore_pressed.png",
    "win_btn_normal":     "gtk-3.0/buttons/window_button_normal.png",
    "win_btn_pressed":    "gtk-3.0/buttons/window_button_pressed.png",
    "scroll_btn":         "gtk-3.0/scrollbar/scrollbar_button.png",
    "stepper_up":         "gtk-3.0/scrollbar/stepper-up.png",
    "stepper_down":       "gtk-3.0/scrollbar/stepper-down.png",
    "stepper_left":       "gtk-3.0/scrollbar/stepper-left.png",
    "stepper_right":      "gtk-3.0/scrollbar/stepper-right.png",
    "stepper_up_active":  "gtk-3.0/scrollbar/stepper-up-active.png",
    "stepper_down_active":"gtk-3.0/scrollbar/stepper-down-active.png",
    "stepper_left_active":"gtk-3.0/scrollbar/stepper-left-active.png",
    "stepper_right_active":"gtk-3.0/scrollbar/stepper-right-active.png",
    "combobox_btn":       "gtk-3.0/buttons/combobox_button_normal.png",
    "combobox_arrow":     "gtk-3.0/assets/combobox-arrow-down.png",
    "expander_plus":      "gtk-3.0/assets/expander_plus.png",
    "expander_minus":     "gtk-3.0/assets/expander_minus.png",
    "arrow_right":        "gtk-3.0/assets/arrow-right.png",
    "arrow_left":         "gtk-3.0/assets/arrow-left.png",
    "arrow_up":           "gtk-3.0/assets/arrow-up.png",
    "arrow_down":         "gtk-3.0/assets/arrow-down.png",
    "handle_h":           "gtk-3.0/assets/handle-h.png",
    "handle_v":           "gtk-3.0/assets/handle-v.png",
    "folder":             "Icons/Chicago95/places/16/folder.png",
    "file":               "Icons/Chicago95/mimes/16/application-octet-stream.png",
}

CROP_BOTTOM = {"combobox_arrow": 16}   # 16x21 -> keep bottom 16 rows

try:
    from PIL import Image
except Exception:
    sys.exit("PIL required: python3 -m pip install --user --break-system-packages pillow")

def load_image(path):
    im = Image.open(path)
    return im.convert("RGBA")

def snap_mono(px):
    grays = all(abs(r - g) <= 8 and abs(g - b) <= 8 and a > 0
                for (r, g, b, a) in px)
    if not grays:
        return [0x00000000 if a < 32 else 0xFF000000 | (r << 16) | (g << 8) | b
                for (r, g, b, a) in px]
    out = []
    for (r, g, b, a) in px:
        if a < 32:
            out.append(0x00000000)
        else:
            lum = (r * 3 + g * 6 + b) // 10
            out.append(0xFFFFFFFF if lum >= 128 else 0xFF000000)
    return out

def cursor_frames(path):
    raw = open(path, "rb").read()
    # ICONDIR: reserved(2) type(2) count(2)
    _res, dtype, count = struct.unpack("<HHH", raw[:6])
    x_hot = y_hot = 0
    if dtype == 2 and count >= 1:
        # ICONDIRENTRY(1): w h colors reserved planes(x_hot) bitcount(y_hot) ...
        _, _, _, _, x_hot, y_hot = struct.unpack("<BBBBHH", raw[6:14])
    im = load_image(path)
    px = list(im.getdata())
    return im.size[0], im.size[1], x_hot, y_hot, snap_mono(px)

def control_frames(name, path):
    im = load_image(path)
    w, h = im.size
    if name in CROP_BOTTOM:
        keep = CROP_BOTTOM[name]
        im = im.crop((0, h - keep, w, h))
        w, h = im.size
    px = list(im.getdata())
    return w, h, snap_mono(px)

def emit_array(vars_):
    out = []
    for x in vars_:
        out.append("0x%08X" % x)
    return ", ".join(out)

def write_header(path, banner, defines, tables, table_name, table_entries):
    lines = [banner, "#ifndef %s" % defines, "#define %s" % defines,
             "", "#include <stdint.h>", ""]
    lines.extend(tables)
    lines.append(table_name)
    lines.extend(table_entries)
    lines.append("};")
    lines.append("#endif")
    open(path, "w", newline="\n").write("\n".join(lines) + "\n")

def main():
    cursors = {}
    for name in CURSOR_ORDER:
        p = os.path.join(THEME, "Cursors", "Chicago95_Standard_Cursors", CURSOR_SRC[name])
        w, h, xh, yh, px = cursor_frames(p)
        cursors[name] = (w, h, xh, yh, px)
        print("ok cursor %-12s %dx%d hot(%d,%d) %s" % (name, w, h, xh, yh, CURSOR_SRC[name]))

    controls = {}
    for name in CONTROL_ORDER:
        if name in ("folder", "file"):
            p = os.path.join(THEME, CONTROL_SRC[name])
        else:
            p = os.path.join(THEME, "Theme", "Chicago95", CONTROL_SRC[name])
        w, h, px = control_frames(name, p)
        controls[name] = (w, h, px)
        print("ok control %-22s %dx%d %s" % (name, w, h, CONTROL_SRC[name]))

    banner = ("/* Auto-generated by scripts/convert_chicago95_full.py - DO NOT EDIT.\n"
              " * Art from the Chicago95 theme pack (https://github.com/grassmunk/Chicago95),\n"
              " * CC BY-SA 4.0. Pixels are 0xAARRGGBB, row-major (top-left first). */")

    # ---- cursors header ----
    tables = []
    arr_tables = []
    for name in CURSOR_ORDER:
        w, h, xh, yh, px = cursors[name]
        arr = emit_array(px)
        arr_tables.append("static const uint32_t %s_px[%d] = {%s};" % (name, len(px), arr))
    tables = ["/* Cursor sprite order matches gui_cursor_t in compositor.h:",
              " * ARROW, HAND, IBEAM, RESIZE_NS, RESIZE_EW, NO, WAIT,", 
              " * RESIZE_NWSE, RESIZE_NESW, MOVE */",
"", "typedef struct {", "    const char *name;", "    uint16_t w, h;",
               "    int16_t x_hot, y_hot;", "    const uint32_t *data;",
              "} pixel_cursor_t;", ""] + arr_tables
    entries = ["static const pixel_cursor_t pixel_cursors[] = {"]
    for name in CURSOR_ORDER:
        w, h, xh, yh, _ = cursors[name]
        entries.append('    {"%s", %d, %d, %d, %d, %s_px},' % (name, w, h, xh, yh, name))
    write_header(os.path.join(OUT_DIR, "pixel_cursors.h"), banner,
                 "PIXEL_CURSORS_H", tables, "static const pixel_cursor_t pixel_cursors[] = {",
                 entries[1:])
    print("wrote pixel_cursors.h")

    # ---- controls header ----
    arr_tables = []
    for name in CONTROL_ORDER:
        w, h, px = controls[name]
        arr = emit_array(px)
        arr_tables.append("static const uint32_t %s_px[%d] = {%s};" % (name, len(px), arr))
    tables = ["typedef struct {", "    const char *name;", "    uint16_t w, h;", "    const uint32_t *data;",
               "} pixel_control_t;", ""] + arr_tables
    entries = ["typedef enum {", "    CONTROL_%s," % CONTROL_ORDER[0].upper()]
    for name in CONTROL_ORDER[1:]:
        entries.append("    CONTROL_%s," % name.upper())
    entries.append("    CONTROL_COUNT")
    entries.append("} pixel_control_id_t;")
    entries.append("")
    entries.append("static const pixel_control_t pixel_controls[] = {")
    for name in CONTROL_ORDER:
        w, h, _ = controls[name]
        entries.append('    {"%s", %d, %d, %s_px},' % (name, w, h, name))
    write_header(os.path.join(OUT_DIR, "pixel_controls.h"), banner,
                 "PIXEL_CONTROLS_H", tables, "",
                 entries)
    print("wrote pixel_controls.h")


if __name__ == "__main__":
    main()