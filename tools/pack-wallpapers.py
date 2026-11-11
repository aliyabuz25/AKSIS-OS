#!/usr/bin/env python3
"""
SHENLONG OS Wallpaper Packer
assets/wallpapers/ içindeki PNG/JPEG dosyalarını
build/out/wallpapers.img disk imajına yazar.

Format:
  Sector 0: Header
    - Magic: "SHLWP\0\0\0" (8 byte)
    - Count: uint32_t (4 byte)
    - Reserved: 4 byte
  Sectors 1..N: Wallpaper entries
    Entry header (512 byte):
      - Name: char[64]
      - Width: uint32_t
      - Height: uint32_t
      - DataOffset: uint32_t (sector numarası)
      - DataSize: uint32_t (byte)
      - Reserved: 432 byte
    Data: width*height*3 byte (RGB, pre-composited on black)
"""

import os, sys, struct
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("PIL bulunamadı: pip install Pillow")
    sys.exit(1)

WALLPAPER_DIR = Path(__file__).parent.parent / "assets" / "wallpapers"
OUTPUT = Path(__file__).parent.parent / "build" / "out" / "wallpapers.img"
SECTOR = 512
# 4K asset 1080p'ye orantili downscale edilir — 1:1 fast-path net HD kalir.
MAX_W, MAX_H = 1920, 1080

def to_sectors(nbytes):
    return (nbytes + SECTOR - 1) // SECTOR

def pack():
    images = sorted([
        f for f in WALLPAPER_DIR.iterdir()
        if f.suffix.lower() in ('.png', '.jpg', '.jpeg', '.bmp')
    ])

    if not images:
        print("Wallpaper bulunamadı — boş imaj yazılıyor")
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        with open(OUTPUT, 'wb') as f:
            f.write(b'\x00' * SECTOR)
        return

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    entries = []
    for img_path in images:
        img = Image.open(img_path).convert('RGBA')
        # Oranı koru, 4K UHD sınırına sığdır
        w, h = img.width, img.height
        if w > MAX_W or h > MAX_H:
            ratio = min(MAX_W/w, MAX_H/h)
            w, h = int(w*ratio), int(h*ratio)
            img = img.resize((w, h), Image.LANCZOS)

        rgb_data = bytearray()
        pixels = img.load()
        for y in range(h):
            for x in range(w):
                r, g, b, a = pixels[x, y]
                rgb_data += bytes([r*a//255, g*a//255, b*a//255])

        entries.append({
            'name': img_path.stem[:63],
            'w': w, 'h': h,
            'data': bytes(rgb_data)
        })
        print(f"  + {img_path.name} → {w}x{h} ({len(rgb_data)//1024}KB)")

    # Offset hesapla
    # Sector 0: magic header
    # Sector 1..N: entry headers (her biri 1 sector)
    # Sonra: data
    cur_sector = 1 + len(entries)
    for e in entries:
        e['data_sector'] = cur_sector
        cur_sector += to_sectors(len(e['data']))

    total_sectors = cur_sector
    img_size = total_sectors * SECTOR

    buf = bytearray(img_size)

    # Sector 0: magic header
    struct.pack_into('<8sI4s', buf, 0,
        b'SHLWP\x00\x00\x00',
        len(entries),
        b'\x00\x00\x00\x00')

    # Entry headers: sector 1..N
    for i, e in enumerate(entries):
        off = (1 + i) * SECTOR
        name_b = e['name'].encode('utf-8')[:63].ljust(64, b'\x00')
        struct.pack_into('<64sIIII', buf, off,
            name_b, e['w'], e['h'],
            e['data_sector'], len(e['data']))

    # Data
    for e in entries:
        off = e['data_sector'] * SECTOR
        buf[off:off+len(e['data'])] = e['data']

    with open(OUTPUT, 'wb') as f:
        f.write(buf)

    print(f"\nwallpapers.img: {img_size//1024}KB, {len(entries)} wallpaper")
    print(f"Çıktı: {OUTPUT}")

if __name__ == '__main__':
    pack()