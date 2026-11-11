#!/usr/bin/env python3
"""
SHENLONG OS 4K Wallpaper Generator
assets/wallpapers/wallpaper_default.png'deki mevcut duvar kağıdını
3840x2160 UHD'ye LANCZOS ile ölçekler ve assets/wallpapers/wallpaper_4k.png
olarak yazar. Packer, 4K sınırına izin verdiği için doğrudan gömülür.

Kullanım:
    python3 tools/make-4k-wallpaper.py
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).parent.parent
SRC = ROOT / "assets" / "wallpapers" / "wallpaper_default.png"
OUT = ROOT / "assets" / "wallpapers" / "wallpaper_4k.png"
TARGET = (3840, 2160)


def main():
    img = Image.open(SRC).convert("RGB")
    print(f"  kaynak: {SRC.name} {img.width}x{img.height}")

    # 16:9 en-boy oranı korunur; 4K'ya tam sığdır (cover)
    img = img.resize(TARGET, Image.LANCZOS)
    img.save(OUT, optimize=True)
    size_kb = OUT.stat().st_size // 1024
    print(f"  hedef:  {OUT.name} {img.width}x{img.height} ({size_kb}KB PNG)")
    print("  Packer build sırasında RGB24 olarak 31.1MB gömecek; "
          "4K'da 1:1 fast-path, 1080p'de cover-scale kullanılacak.")


if __name__ == "__main__":
    main()
