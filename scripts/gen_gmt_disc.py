#!/usr/bin/env python3
"""Generate the GMT disc face bitmap for Sky watchface.

Creates a transparent PNG with 24-hour numbers arranged in a circle,
each oriented radially. This bitmap gets rotated at runtime by
graphics_draw_rotated_bitmap() to show the current GMT hour.

Uses a large bold font and nearest-neighbor rotation to stay sharp
on the Pebble's low-res 64-color display.
"""

import math
import os
from PIL import Image, ImageDraw, ImageFont

# Must match constants in main.c
GMT_RING_OUTER = 73
GMT_RING_INNER = 49
GMT_NUM_R = 61

# Render at 2x then downscale for cleaner edges
SCALE = 2
FINAL_SIZE = 2 * GMT_RING_OUTER + 4  # 150
SIZE = FINAL_SIZE * SCALE
CENTER = SIZE // 2
NUM_R = GMT_NUM_R * SCALE

out_dir = os.path.join(os.path.dirname(__file__), '..', 'resources')
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, 'gmt_disc.png')

img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Bold font at scaled size
font = None
for fp in [
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Geneva.ttf",
]:
    if os.path.exists(fp):
        try:
            font = ImageFont.truetype(fp, 18 * SCALE)
            break
        except Exception:
            continue
if font is None:
    font = ImageFont.load_default()

# Dots at odd hours
DOT_R = GMT_NUM_R * SCALE
DOT_SIZE = 2 * SCALE
for h in range(0, 24):
    if h % 2 == 0:
        continue
    angle_rad = math.radians(h * 360 / 24)
    x = CENTER + DOT_R * math.sin(angle_rad)
    y = CENTER - DOT_R * math.cos(angle_rad)
    draw.ellipse([(x - DOT_SIZE, y - DOT_SIZE), (x + DOT_SIZE, y + DOT_SIZE)],
                 fill=(255, 255, 255, 255))

# Numbers at even hours, each rotated radially
for h in range(0, 24, 2):
    angle_deg = h * 360 / 24
    angle_rad = math.radians(angle_deg)

    x = CENTER + NUM_R * math.sin(angle_rad)
    y = CENTER - NUM_R * math.cos(angle_rad)

    text = str(24 if h == 0 else h)

    # Render text on a generous canvas
    txt_size = (32 * SCALE, 20 * SCALE)
    txt_img = Image.new('RGBA', txt_size, (0, 0, 0, 0))
    txt_draw = ImageDraw.Draw(txt_img)

    bbox = txt_draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (txt_size[0] - tw) // 2 - bbox[0]
    ty = (txt_size[1] - th) // 2 - bbox[1]
    txt_draw.text((tx, ty), text, fill=(255, 255, 255, 255), font=font)

    # Rotate with nearest-neighbor for sharp pixels
    rotated = txt_img.rotate(-angle_deg, expand=True, resample=Image.NEAREST)

    paste_x = int(x - rotated.width / 2)
    paste_y = int(y - rotated.height / 2)
    img.paste(rotated, (paste_x, paste_y), rotated)

# Downscale to final size with LANCZOS for smooth edges
img = img.resize((FINAL_SIZE, FINAL_SIZE), Image.LANCZOS)

img.save(out_path)
print(f"Created {out_path} ({FINAL_SIZE}x{FINAL_SIZE})")
