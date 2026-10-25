#!/usr/bin/env python3
"""把 spritesheet.webp 切成单帧透明 PNG。

用法：python3 scripts/slice_spritesheet.py [输出目录]
默认输出到 assets/frames/（已在 .gitignore 中）。需要 Pillow。
文件名：<动作>/<序号>.png。视线方向文件名用角度，如 look/022.5.png。
"""
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHEET = ROOT / "pet" / "heidou" / "spritesheet.webp"
CELL_W, CELL_H = 192, 208

# (目录, 帧数, 文件名列表或 None)。顺序即图集的行序。
LOOK_ROW_9 = ["000", "022.5", "045", "067.5", "090", "112.5", "135", "157.5"]
LOOK_ROW_10 = ["180", "202.5", "225", "247.5", "270", "292.5", "315", "337.5"]
ROWS = [
    ("idle", 6, None),
    ("running-right", 8, None),
    ("running-left", 8, None),
    ("waving", 4, None),
    ("jumping", 5, None),
    ("failed", 8, None),
    ("waiting", 6, None),
    ("running", 6, None),
    ("review", 6, None),
    ("look", 8, LOOK_ROW_9),
    ("look", 8, LOOK_ROW_10),
]


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "assets" / "frames"
    count = 0
    with Image.open(SHEET) as im:
        im = im.convert("RGBA")
        for row, (name, frames, labels) in enumerate(ROWS):
            (out / name).mkdir(parents=True, exist_ok=True)
            for col in range(frames):
                box = (col * CELL_W, row * CELL_H, (col + 1) * CELL_W, (row + 1) * CELL_H)
                label = labels[col] if labels else f"{col:02d}"
                im.crop(box).save(out / name / f"{label}.png")
                count += 1
        # v2 图集在 idle 行第 6 列放视线的中立（正面）帧。
        (out / "look").mkdir(parents=True, exist_ok=True)
        neutral = (6 * CELL_W, 0, 7 * CELL_W, CELL_H)
        im.crop(neutral).save(out / "look" / "neutral.png")
        count += 1
    print(f"已切出 {count} 帧到 {out}")


if __name__ == "__main__":
    main()
