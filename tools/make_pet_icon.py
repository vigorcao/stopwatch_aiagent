"""Launcher icon from a Codex v2 pet: first idle frame on a cream disc, 200x200 RGB565.
Usage: make_pet_icon.py <codex-pet-dir> <icon-name> <out.c> [preview.png]"""
import json, os, sys
from PIL import Image, ImageDraw
from lv_icon import write_rgb565_c

src, name, out = sys.argv[1], sys.argv[2], sys.argv[3]
meta = json.load(open(os.path.join(src, "pet.json")))
sheet = Image.open(os.path.join(src, meta["spritesheetPath"])).convert("RGBA")
cell = sheet.crop((0, 0, 192, 208))
cell = cell.crop(cell.getbbox())  # trim transparent margin
scale = 150 / max(cell.size)
cell = cell.resize((round(cell.width * scale), round(cell.height * scale)), Image.LANCZOS)

S = 4
disc = Image.new("RGBA", (200 * S, 200 * S), (0, 0, 0, 255))
ImageDraw.Draw(disc).ellipse([6 * S, 6 * S, 194 * S, 194 * S], fill=(240, 236, 226, 255))
im = disc.resize((200, 200), Image.LANCZOS)
im.alpha_composite(cell, ((200 - cell.width) // 2, (200 - cell.height) // 2 + 4))
if len(sys.argv) > 4:
    im.save(sys.argv[4])
write_rgb565_c(im.convert("RGB"), name, out, "make_pet_icon.py")
