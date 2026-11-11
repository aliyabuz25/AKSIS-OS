#!/usr/bin/env python3
import sys, struct
from PIL import Image

src = sys.argv[1]
dst = sys.argv[2]

img = Image.open(src).convert('RGBA')
w2 = 480
h2 = int(w2 * img.height / img.width)
img = img.resize((w2, h2), Image.LANCZOS)
pix = list(img.getdata())
data = struct.pack('<II', w2, h2)
for r, g, b, a in pix:
    data += bytes([r*a//255, g*a//255, b*a//255])
with open(dst, 'wb') as f:
    f.write(data)
print(f'panic-logo.rgb: {w2}x{h2}, {len(data)} bytes')
