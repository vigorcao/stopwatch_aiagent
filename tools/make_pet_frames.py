"""Convert a Codex v2 pet (pet.json + 1536x2288 spritesheet) into LVGL .bin frames.

Usage: make_pet_frames.py <codex-pet-dir> <out-dir> [scale] [--rle | --lz4] [--bg=RRGGBB] [--despill] [--i8]

Rows follow the Codex V2 pet contract (8 cols x 11 rows, 192x208 cells).
--lz4 writes LVGL LZ4-compressed frames (needs LV_USE_LZ4, pip install lz4); about
40% smaller than --rle on the same pets, and it keeps working on furry/detailed art.
--i8 writes 256-colour indexed frames (LVGL I8, one palette shared by the whole
pet, Floyd-Steinberg dithered): 1 byte/px, for detailed pets where RLE alone saves little.
Alpha is flattened onto the watch app background so frames are plain RGB565
(2 bytes/px) and fit the 4 MB storage partition. --rle writes LVGL RLE-compressed
frames (needs LV_USE_RLE); flat cartoon pets shrink several times.
"""
import json, os, struct, sys
from PIL import Image

BG = (240, 236, 226)  # must match the GPT app background colour
CELL_W, CELL_H = 192, 208
ROWS = {  # state: (row, frame count)
    "idle": (0, 6), "walk": (1, 8), "waving": (3, 4), "jumping": (4, 5), "failed": (5, 8),
    "waiting": (6, 6), "running": (7, 6), "review": (8, 6),
}
LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RGB565, LV_COLOR_FORMAT_I8 = 0x19, 0x12, 0x0A
LV_IMAGE_FLAGS_COMPRESSED, LV_IMAGE_COMPRESS_RLE, LV_IMAGE_COMPRESS_LZ4 = 0x08, 1, 2


def rle(px, blk=2):
    """LVGL lv_rle format: ctrl < 0x80 repeats the next block ctrl times, ctrl | 0x80 copies ctrl & 0x7f blocks."""
    blocks = [px[i:i + blk] for i in range(0, len(px), blk)]
    out, i, n = bytearray(), 0, len(blocks)
    while i < n:
        run = 1
        while i + run < n and run < 127 and blocks[i + run] == blocks[i]:
            run += 1
        if run >= 3:
            out += bytes([run]) + blocks[i]
            i += run
            continue
        start = i
        while i < n and i - start < 127:
            if i + 2 < n and blocks[i] == blocks[i + 1] == blocks[i + 2]:
                break
            i += 1
        out += bytes([0x80 | (i - start)]) + b"".join(blocks[start:i])
    return bytes(out)


def write_bin(im, path, compress=False, palette=None):
    w, h = im.size
    px = bytearray()
    if palette is not None:  # I8: 256 x lv_color32_t (B, G, R, A), then one index byte per pixel
        q = im.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)
        pal = q.getpalette()[:768]
        pal += [0] * (768 - len(pal))
        for i in range(256):
            px += bytes((pal[i * 3 + 2], pal[i * 3 + 1], pal[i * 3], 255))
        px += q.tobytes()
        cf, stride, blk = LV_COLOR_FORMAT_I8, w, 1
    else:
        for r, g, b in im.getdata():
            px += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        cf, stride, blk = LV_COLOR_FORMAT_RGB565, w * 2, 2
    flags = LV_IMAGE_FLAGS_COMPRESSED if compress else 0
    header = struct.pack("<BBHHHHH", LV_IMAGE_HEADER_MAGIC, cf, flags, w, h, stride, 0)
    if compress == "lz4":
        import lz4.block
        data = lz4.block.compress(bytes(px), mode="high_compression", compression=12, store_size=False)
        body = struct.pack("<III", LV_IMAGE_COMPRESS_LZ4, len(data), len(px)) + data
    elif compress:
        data = rle(bytes(px), blk)
        body = struct.pack("<III", LV_IMAGE_COMPRESS_RLE, len(data), len(px)) + data
    else:
        body = bytes(px)
    with open(path, "wb") as f:
        f.write(header + body)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    compress = "lz4" if "--lz4" in sys.argv else "--rle" in sys.argv
    despill = "--despill" in sys.argv
    global BG
    for a in sys.argv[1:]:
        if a.startswith("--bg="):  # must match the PetView bg colour of that app
            BG = tuple(bytes.fromhex(a[5:]))
    src, out = args[0], args[1]
    scale = float(args[2]) if len(args) > 2 else 0.8
    meta = json.load(open(os.path.join(src, "pet.json")))
    assert meta.get("spriteVersionNumber") == 2, "only Codex v2 pets are supported"
    sheet = Image.open(os.path.join(src, meta["spritesheetPath"])).convert("RGBA")
    os.makedirs(out, exist_ok=True)
    size = (round(CELL_W * scale), round(CELL_H * scale))

    def cell(row, col):
        c = sheet.crop((col * CELL_W, row * CELL_H, (col + 1) * CELL_W, (row + 1) * CELL_H))
        if despill:  # soft edges keep a cyan tint from the chroma-key background
            px = c.load()
            for y in range(c.height):
                for x in range(c.width):
                    r, g, b, a = px[x, y]
                    if 0 < a < 250:
                        px[x, y] = (r, min(g, r + 12), min(b, r + 12), a)
        flat = Image.new("RGBA", c.size, BG + (255,))
        flat.alpha_composite(c)
        return flat.convert("RGB").resize(size, Image.LANCZOS)

    frames = {}
    for state, (row, n) in ROWS.items():
        for i in range(n):
            frames[f"{state}_{i}"] = cell(row, i)
    for i in range(16):  # look directions, clockwise from 12 o'clock
        frames[f"look_{i}"] = cell(9 + i // 8, i % 8)

    palette = None
    if "--i8" in sys.argv:  # one palette from every frame, so colours stay stable across frames
        strip = Image.new("RGB", (size[0], size[1] * len(frames)))
        for k, im in enumerate(frames.values()):
            strip.paste(im, (0, k * size[1]))
        palette = strip.quantize(colors=256, method=Image.Quantize.MEDIANCUT)

    for name, im in frames.items():
        write_bin(im, os.path.join(out, f"{name}.bin"), compress, palette)
    count = len(frames)
    total = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out))
    print(f"{meta['displayName']}: {count} frames at {size[0]}x{size[1]} -> {out} ({total / 1024:.0f} KB)")


main()
