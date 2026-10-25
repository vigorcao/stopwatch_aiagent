#!/usr/bin/env python3
"""检查 Codex v2 pet 包是否符合规格。

用法：python3 scripts/validate_pet.py [pet 目录]
默认检查 pet/heidou。需要 Pillow。
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image

COLUMNS, ROWS = 8, 11
CELL_W, CELL_H = 192, 208
ATLAS_W, ATLAS_H = COLUMNS * CELL_W, ROWS * CELL_H  # 1536 x 2288

# 每个标准动作的帧数。其余格子必须全透明。
FRAMES_PER_ROW = [6, 8, 8, 4, 5, 8, 6, 6, 6, 8, 8]
# v2 图集在 idle 行的第 6 列放视线的中立（正面）帧，所以这一格必须有内容。
NEUTRAL_CELL = (0, 6)


def validate(pet_dir: Path) -> list[str]:
    errors = []

    manifest_path = pet_dir / "pet.json"
    if not manifest_path.is_file():
        return [f"缺少 {manifest_path}"]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    for key in ("id", "displayName", "description", "spriteVersionNumber", "spritesheetPath"):
        if key not in manifest:
            errors.append(f"pet.json 缺少字段 {key}")
    if manifest.get("spriteVersionNumber") != 2:
        errors.append("spriteVersionNumber 必须是 2。缺省值是 1，Codex 会拒绝 2288 像素高的图集。")

    sheet_path = pet_dir / manifest.get("spritesheetPath", "spritesheet.webp")
    if not sheet_path.is_file():
        return errors + [f"缺少图集 {sheet_path}"]

    with Image.open(sheet_path) as im:
        im = im.convert("RGBA")
        if im.size != (ATLAS_W, ATLAS_H):
            return errors + [f"图集尺寸是 {im.size}，应为 {(ATLAS_W, ATLAS_H)}"]
        alpha = im.getchannel("A")

        for row, frames in enumerate(FRAMES_PER_ROW):
            for col in range(COLUMNS):
                box = (col * CELL_W, row * CELL_H, (col + 1) * CELL_W, (row + 1) * CELL_H)
                empty = alpha.crop(box).getbbox() is None
                if (row, col) == NEUTRAL_CELL:
                    if empty:
                        errors.append("中立帧（第 0 行第 6 列）全透明")
                    continue
                if col < frames and empty:
                    errors.append(f"第 {row} 行第 {col} 列应有内容，但全透明")
                if col >= frames and not empty:
                    errors.append(f"第 {row} 行第 {col} 列应为空，但有像素")
    return errors


def main() -> int:
    pet_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "pet" / "heidou"
    errors = validate(pet_dir)
    if errors:
        print(f"校验失败：{pet_dir}")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"校验通过：{pet_dir}（{ATLAS_W}x{ATLAS_H}，{COLUMNS}x{ROWS} 格，v2）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
