#!/usr/bin/env python3
"""Encode the supplied artwork losslessly as a tiny header + packed RGB24.

No runtime PNG decoder, generated C arrays or color quantization are needed.
The original PNG remains the source of truth in assets/boot/dragon.png.
"""
import argparse
from pathlib import Path
import struct
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
with Image.open(args.source) as source:
    rgb = source.convert('RGB')
    if not (0 < rgb.width <= 4096 and 0 < rgb.height <= 4096):
        raise SystemExit('Boot artwork must be between 1 and 4096 pixels per axis.')
    args.output.write_bytes(struct.pack('<II', *rgb.size) + rgb.tobytes())
    print(f'Boot artwork: {rgb.width}x{rgb.height}, lossless RGB24')
