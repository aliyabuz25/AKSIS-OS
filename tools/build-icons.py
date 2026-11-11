#!/usr/bin/env python3
"""Build authentic macOS-style app icons for SHENLONG OS.

Generates high-resolution 256x256 master icons using PIL with rich gradients,
squircles, bevel highlights, drop shadows, and anti-aliasing, then resizes
them to standard kernel icon sizes (20, 22, 24, 32, 40, 48, 56, 64) and
outputs C arrays in kernel/app_icons_data.h.
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
ASSETS_BOOT = ROOT / "assets/boot"
OUT_HEADER = ROOT / "kernel/app_icons_data.h"
SIZES = [20, 22, 24, 32, 40, 48, 56, 64]
N = 256

def squircle_mask(size, radius):
    im = Image.new("L", (size * 4, size * 4), 0)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, size * 4 - 1, size * 4 - 1], radius=radius * 4, fill=255)
    return im.resize((size, size), Image.Resampling.LANCZOS)

def vertical_gradient(w, h, top_color, bottom_color):
    base = Image.new("RGBA", (w, h))
    pixels = base.load()
    for y in range(h):
        t = y / max(1, h - 1)
        r = int(top_color[0] + (bottom_color[0] - top_color[0]) * t)
        g = int(top_color[1] + (bottom_color[1] - top_color[1]) * t)
        b = int(top_color[2] + (bottom_color[2] - top_color[2]) * t)
        a = int(top_color[3] + (bottom_color[3] - top_color[3]) * t)
        for x in range(w):
            pixels[x, y] = (r, g, b, a)
    return base

def apply_squircle_tile(base, top_c, bot_c, radius=56):
    bg = vertical_gradient(N, N, top_c, bot_c)
    mask = squircle_mask(N, radius)
    
    # Outer drop shadow
    shadow_mask = mask.filter(ImageFilter.GaussianBlur(8))
    shadow = Image.new("RGBA", (N, N), (0, 0, 0, 90))
    base.paste(shadow, (0, 4), shadow_mask)
    
    # Squircle body
    base.paste(bg, (0, 0), mask)
    
    # Top glass bevel highlight
    bevel = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bevel)
    bd.rounded_rectangle([2, 2, N - 3, N - 3], radius=radius, outline=(255, 255, 255, 60), width=3)
    bd.rounded_rectangle([3, 3, N - 4, (N // 2)], radius=radius, outline=(255, 255, 255, 35), width=2)
    base.paste(bevel, (0, 0), mask)
    
    # Dark border
    border = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    bd2 = ImageDraw.Draw(border)
    bd2.rounded_rectangle([0, 0, N - 1, N - 1], radius=radius, outline=(0, 0, 0, 90), width=2)
    base.paste(border, (0, 0), mask)

def make_terminal():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (45, 52, 64, 255), (18, 20, 26, 255))
    
    d = ImageDraw.Draw(im)
    # Header line
    d.line([(24, 60), (N - 24, 60)], fill=(60, 68, 80, 200), width=2)
    # macOS window traffic dots
    d.ellipse([32, 34, 46, 48], fill=(255, 95, 86, 240))  # Red
    d.ellipse([54, 34, 68, 48], fill=(255, 189, 46, 240)) # Yellow
    d.ellipse([76, 34, 90, 48], fill=(39, 201, 63, 240))  # Green
    
    # Terminal Prompt: Bold chevron '>' in glowing emerald
    chevron = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    cd = ImageDraw.Draw(chevron)
    cd.line([(50, 100), (96, 140)], fill=(52, 211, 153, 255), width=14)
    cd.line([(96, 140), (50, 180)], fill=(52, 211, 153, 255), width=14)
    cd.ellipse([43, 93, 57, 107], fill=(52, 211, 153, 255))
    cd.ellipse([89, 133, 103, 147], fill=(52, 211, 153, 255))
    cd.ellipse([43, 173, 57, 187], fill=(52, 211, 153, 255))
    
    # Cursor: Bold block cursor
    cd.rounded_rectangle([116, 166, 170, 182], radius=4, fill=(56, 189, 248, 255))
    
    glow = chevron.filter(ImageFilter.GaussianBlur(6))
    im.paste(glow, (0, 0), glow)
    im.paste(chevron, (0, 0), chevron)
    return im

def make_finder():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    mask = squircle_mask(N, 56)
    
    left_bg = vertical_gradient(N, N, (56, 189, 248, 255), (2, 132, 199, 255))
    right_bg = vertical_gradient(N, N, (2, 132, 199, 255), (29, 78, 216, 255))
    
    half_mask = Image.new("L", (N, N), 0)
    hd = ImageDraw.Draw(half_mask)
    hd.rectangle([0, 0, N // 2, N], fill=255)
    hd.ellipse([N // 2 - 20, N // 2 - 24, N // 2 + 20, N // 2 + 24], fill=255)
    
    bg = Image.composite(left_bg, right_bg, half_mask)
    apply_squircle_tile(im, (56, 189, 248, 255), (29, 78, 216, 255))
    im.paste(bg, (0, 0), mask)
    
    bevel = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bevel)
    bd.rounded_rectangle([2, 2, N - 3, N - 3], radius=56, outline=(255, 255, 255, 80), width=3)
    im.paste(bevel, (0, 0), mask)
    
    border = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    bd2 = ImageDraw.Draw(border)
    bd2.rounded_rectangle([0, 0, N - 1, N - 1], radius=56, outline=(0, 0, 0, 90), width=2)
    im.paste(border, (0, 0), mask)
    
    face = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    fd = ImageDraw.Draw(face)
    fd.line([(N // 2, 40), (N // 2, N // 2 - 24)], fill=(15, 23, 42, 220), width=5)
    fd.arc([N // 2 - 20, N // 2 - 24, N // 2 + 20, N // 2 + 24], start=-90, end=90, fill=(15, 23, 42, 220), width=5)
    fd.line([(N // 2, N // 2 + 24), (N // 2, N - 85)], fill=(15, 23, 42, 220), width=5)
    
    fd.ellipse([70, 92, 88, 128], fill=(15, 23, 42, 230))
    fd.ellipse([N - 88, 92, N - 70, 128], fill=(15, 23, 42, 230))
    fd.ellipse([73, 96, 81, 108], fill=(255, 255, 255, 180))
    fd.ellipse([N - 85, 96, N - 77, 108], fill=(255, 255, 255, 180))
    
    fd.arc([52, N - 148, N - 52, N - 62], start=20, end=160, fill=(15, 23, 42, 230), width=7)
    fd.ellipse([50, N - 110, 60, N - 100], fill=(15, 23, 42, 230))
    fd.ellipse([N - 60, N - 110, N - 50, N - 100], fill=(15, 23, 42, 230))
    
    im.paste(face, (0, 0), face)
    return im

def make_settings():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (82, 92, 108, 255), (30, 36, 46, 255))
    
    gear = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gear)
    cx, cy = N // 2, N // 2
    r_outer = 84
    r_inner = 68
    r_hole = 30
    teeth = 8
    
    for i in range(teeth):
        angle = (i * 360 / teeth) * (math.pi / 180)
        tooth_w = 26
        cos_a = math.cos(angle)
        sin_a = math.sin(angle)
        tx = cx + (r_outer - 2) * cos_a
        ty = cy + (r_outer - 2) * sin_a
        gd.rounded_rectangle([tx - tooth_w/2, ty - tooth_w/2, tx + tooth_w/2, ty + tooth_w/2], radius=4, fill=(226, 232, 240, 255))
    
    gd.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer], fill=(203, 213, 225, 255))
    gd.ellipse([cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner], fill=(148, 163, 184, 255))
    gd.ellipse([cx - r_inner + 8, cy - r_inner + 8, cx + r_inner - 8, cy + r_inner - 8], fill=(203, 213, 225, 255))
    gd.ellipse([cx - r_hole, cy - r_hole, cx + r_hole, cy + r_hole], fill=(30, 36, 46, 255))
    gd.ellipse([cx - 10, cy - 10, cx + 10, cy + 10], fill=(148, 163, 184, 255))
    
    g_shadow = gear.filter(ImageFilter.GaussianBlur(8))
    im.paste(g_shadow, (0, 4), g_shadow)
    im.paste(gear, (0, 0), gear)
    return im

def make_browser():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (255, 255, 255, 255), (241, 245, 249, 255))
    cx, cy = N // 2, N // 2
    r_disc = 92
    
    disc_grad = vertical_gradient(N, N, (14, 165, 233, 255), (29, 78, 216, 255))
    disc_mask = Image.new("L", (N, N), 0)
    dmd = ImageDraw.Draw(disc_mask)
    dmd.ellipse([cx - r_disc, cy - r_disc, cx + r_disc, cy + r_disc], fill=255)
    
    disc_shadow = disc_mask.filter(ImageFilter.GaussianBlur(6))
    im.paste(Image.new("RGBA", (N, N), (0, 0, 0, 60)), (0, 4), disc_shadow)
    im.paste(disc_grad, (0, 0), disc_mask)
    
    ticks = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    td = ImageDraw.Draw(ticks)
    for a_deg in range(0, 360, 15):
        rad = a_deg * math.pi / 180
        is_cardinal = (a_deg % 90 == 0)
        l = 10 if is_cardinal else 6
        r1 = r_disc - 6
        r2 = r_disc - 6 - l
        x1 = cx + r1 * math.cos(rad)
        y1 = cy + r1 * math.sin(rad)
        x2 = cx + r2 * math.cos(rad)
        y2 = cy + r2 * math.sin(rad)
        td.line([(x1, y1), (x2, y2)], fill=(255, 255, 255, 200 if is_cardinal else 120), width=3 if is_cardinal else 2)
    im.paste(ticks, (0, 0), ticks)
    
    needle = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    nd = ImageDraw.Draw(needle)
    cos45 = math.cos(math.pi / 4)
    sin45 = math.sin(math.pi / 4)
    vx, vy = cos45, -sin45
    nx, ny = sin45, cos45
    
    tip_red = (cx + 66 * vx, cy + 66 * vy)
    tip_white = (cx - 66 * vx, cy - 66 * vy)
    w_side1 = (cx + 14 * nx, cy + 14 * ny)
    w_side2 = (cx - 14 * nx, cy - 14 * ny)
    
    nd.polygon([tip_red, w_side1, (cx, cy)], fill=(248, 113, 113, 255))
    nd.polygon([tip_red, w_side2, (cx, cy)], fill=(239, 68, 68, 255))
    nd.polygon([tip_white, w_side1, (cx, cy)], fill=(255, 255, 255, 255))
    nd.polygon([tip_white, w_side2, (cx, cy)], fill=(203, 213, 225, 255))
    
    nd.ellipse([cx - 10, cy - 10, cx + 10, cy + 10], fill=(241, 245, 249, 255), outline=(100, 116, 139, 200), width=2)
    nd.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(239, 68, 68, 255))
    
    n_shadow = needle.filter(ImageFilter.GaussianBlur(5))
    im.paste(n_shadow, (0, 3), n_shadow)
    im.paste(needle, (0, 0), needle)
    return im

def make_network():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (139, 92, 246, 255), (67, 56, 202, 255))
    cx, cy = N // 2, N // 2 + 10
    
    waves = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    wd = ImageDraw.Draw(waves)
    for r in [40, 70, 100]:
        wd.arc([cx - r, cy - r - 20, cx + r, cy + r - 20], start=210, end=330, fill=(255, 255, 255, 140 - r), width=7)
    
    wd.ellipse([cx - 16, cy - 26, cx + 16, cy + 6], fill=(255, 255, 255, 255))
    wd.ellipse([cx - 8, cy - 18, cx + 8, cy - 2], fill=(129, 140, 248, 255))
    
    nodes = [(cx - 65, cy + 48), (cx, cy + 65), (cx + 65, cy + 48)]
    for nx, ny in nodes:
        wd.line([(cx, cy - 10), (nx, ny)], fill=(199, 210, 254, 200), width=4)
        wd.ellipse([nx - 14, ny - 14, nx + 14, ny + 14], fill=(255, 255, 255, 255), outline=(99, 102, 241, 255), width=3)
    
    w_glow = waves.filter(ImageFilter.GaussianBlur(6))
    im.paste(w_glow, (0, 0), w_glow)
    im.paste(waves, (0, 0), waves)
    return im

def make_media():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (39, 39, 42, 255), (9, 9, 11, 255))
    cx, cy = N // 2, N // 2
    
    emblem = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ed = ImageDraw.Draw(emblem)
    r_outer = 82
    r_inner = 56
    
    for a in range(0, 360, 2):
        rad = a * math.pi / 180
        t = (a / 360.0)
        cr = int(34 + (59 - 34) * t)
        cg = int(211 + (130 - 211) * t)
        cb = int(238 + (246 - 238) * t)
        x1 = cx + (r_outer - 4) * math.cos(rad)
        y1 = cy + (r_outer - 4) * math.sin(rad)
        x2 = cx + (r_inner + 4) * math.cos(rad)
        y2 = cy + (r_inner + 4) * math.sin(rad)
        ed.line([(x1, y1), (x2, y2)], fill=(cr, cg, cb, 255), width=4)
    
    ed.rounded_rectangle([cx + 28, cy + 34, cx + 78, cy + 84], radius=16, fill=(34, 211, 238, 255))
    tri_pts = [(cx - 18, cy - 32), (cx + 34, cy), (cx - 18, cy + 32)]
    ed.polygon(tri_pts, fill=(255, 255, 255, 255))
    
    e_glow = emblem.filter(ImageFilter.GaussianBlur(8))
    im.paste(e_glow, (0, 0), e_glow)
    im.paste(emblem, (0, 0), emblem)
    return im

def make_folder():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    # macOS Blue Folder
    fd = ImageDraw.Draw(im)
    
    # Back tab
    fd.rounded_rectangle([28, 48, 120, 100], radius=14, fill=(56, 189, 248, 255))
    fd.rounded_rectangle([28, 64, N - 28, N - 48], radius=24, fill=(2, 132, 199, 255))
    
    # Front flap
    front = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    frd = ImageDraw.Draw(front)
    frd.rounded_rectangle([24, 88, N - 24, N - 44], radius=22, fill=(56, 189, 248, 255), outline=(125, 211, 252, 255), width=2)
    fr_shadow = front.filter(ImageFilter.GaussianBlur(6))
    im.paste(fr_shadow, (0, 4), fr_shadow)
    im.paste(front, (0, 0), front)
    return im

def make_document():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    # Sheet
    sheet = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sheet)
    # Fold corner at (N-50, 40)
    pts = [(50, 36), (N - 90, 36), (N - 50, 76), (N - 50, N - 36), (50, N - 36)]
    sd.polygon(pts, fill=(255, 255, 255, 255), outline=(203, 213, 225, 255))
    # Dog-ear fold
    sd.polygon([(N - 90, 36), (N - 50, 76), (N - 90, 76)], fill=(226, 232, 240, 255), outline=(148, 163, 184, 255))
    # Text lines
    for ly in [104, 132, 160, 188]:
        sd.rounded_rectangle([74, ly, N - 74, ly + 10], radius=4, fill=(148, 163, 184, 200))
    sd.rounded_rectangle([74, 216, N - 120, 226], radius=4, fill=(148, 163, 184, 200))
    
    s_shadow = sheet.filter(ImageFilter.GaussianBlur(8))
    im.paste(s_shadow, (0, 4), s_shadow)
    im.paste(sheet, (0, 0), sheet)
    return im

def make_image():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (56, 189, 248, 255), (14, 116, 144, 255))
    # Sun
    id_draw = ImageDraw.Draw(im)
    id_draw.ellipse([N - 95, 55, N - 55, 95], fill=(251, 191, 36, 255))
    # Mountains
    m1 = [(30, N - 40), (105, 120), (175, N - 40)]
    id_draw.polygon(m1, fill=(15, 118, 110, 255))
    m2 = [(115, N - 40), (185, 145), (N - 30, N - 40)]
    id_draw.polygon(m2, fill=(13, 148, 136, 255))
    return im

def make_executable():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (30, 41, 59, 255), (15, 23, 42, 255))
    ed = ImageDraw.Draw(im)
    cx, cy = N // 2, N // 2
    # Chip circuit icon
    ed.rounded_rectangle([cx - 48, cy - 48, cx + 48, cy + 48], radius=16, fill=(51, 65, 85, 255), outline=(56, 189, 248, 255), width=4)
    ed.ellipse([cx - 20, cy - 20, cx + 20, cy + 20], fill=(56, 189, 248, 255))
    for i in [-30, 0, 30]:
        ed.line([(cx + i, cy - 48), (cx + i, cy - 64)], fill=(56, 189, 248, 255), width=4)
        ed.line([(cx + i, cy + 48), (cx + i, cy + 64)], fill=(56, 189, 248, 255), width=4)
        ed.line([(cx - 48, cy + i), (cx - 64, cy + i)], fill=(56, 189, 248, 255), width=4)
        ed.line([(cx + 48, cy + i), (cx + 64, cy + i)], fill=(56, 189, 248, 255), width=4)
    return im

def make_dragon():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    apply_squircle_tile(im, (51, 65, 85, 240), (15, 23, 42, 240))
    
    dragon_path = ASSETS_BOOT / "dragon.png"
    if dragon_path.exists():
        with Image.open(dragon_path) as dim:
            d_rgba = dim.convert("RGBA")
            w_dragon = Image.new("RGBA", d_rgba.size, (255, 255, 255, 255))
            w_dragon.putalpha(d_rgba.getchannel("A") if "A" in d_rgba.getbands() else d_rgba.convert("L"))
            dw = 160
            dh = int(dw * d_rgba.height / d_rgba.width)
            w_dragon = w_dragon.resize((dw, dh), Image.Resampling.LANCZOS)
            
            dx = (N - dw) // 2
            dy = (N - dh) // 2
            
            dglow = w_dragon.filter(ImageFilter.GaussianBlur(6))
            im.paste(dglow, (dx, dy), dglow)
            im.paste(w_dragon, (dx, dy), w_dragon)
    return im

def generate():
    makers = [
        ("terminal", make_terminal),     # 0
        ("finder", make_finder),         # 1
        ("settings", make_settings),     # 2
        ("browser", make_browser),       # 3
        ("network", make_network),       # 4
        ("media", make_media),           # 5
        ("folder", make_folder),         # 6
        ("document", make_document),     # 7
        ("image", make_image),           # 8
        ("executable", make_executable), # 9
        ("dragon", make_dragon),         # 10
    ]
    
    print("Generating master 256x256 macOS icons...")
    masters = []
    for name, fn in makers:
        im = fn()
        masters.append((name, im))
        im.save(ROOT / f"build/out/icon_{name}.png")
        print(f"  - {name} generated")
    
    print(f"Rendering multi-resolution tables for {SIZES}...")
    lines = [
        "/* Auto-generated macOS App Icons for SHENLONG OS */",
        "#ifndef SHENLONG_APP_ICONS_DATA_H",
        "#define SHENLONG_APP_ICONS_DATA_H",
        '#include "libc/stdint.h"',
        "",
    ]
    
    for app_idx, (name, master) in enumerate(masters):
        for s in SIZES:
            scaled = master.resize((s, s), Image.Resampling.LANCZOS)
            pixels = scaled.load()
            lines.append(f"static const uint32_t icon_{name}_{s}[{s}*{s}] = {{")
            row = []
            for y in range(s):
                for x in range(s):
                    r, g, b, a = pixels[x, y]
                    val = (a << 24) | (r << 16) | (g << 8) | b
                    row.append(f"0x{val:08x}u")
                lines.append("    " + ", ".join(row) + ",")
                row = []
            lines.append("};")
            lines.append("")
            
    # Array table lookups: 11 icons, 8 sizes
    lines.append("static const uint32_t *const app_icon_tables[11][8] = {")
    for app_idx, (name, _) in enumerate(masters):
        ptrs = [f"icon_{name}_{s}" for s in SIZES]
        lines.append(f"    {{ {', '.join(ptrs)} }}, /* {app_idx}: {name} */")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* SHENLONG_APP_ICONS_DATA_H */")
    
    OUT_HEADER.write_text("\n".join(lines))
    print(f"Wrote {OUT_HEADER} ({len(lines)} lines)")

if __name__ == "__main__":
    generate()
