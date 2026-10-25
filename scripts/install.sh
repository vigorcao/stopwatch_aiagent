#!/usr/bin/env bash
# 把黑豆安装到 Codex 的自定义 pet 目录。
# 用法：scripts/install.sh [目标目录]
# 默认目标：${CODEX_HOME:-$HOME/.codex}/pets/heidou
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/pet/heidou"
DEST="${1:-${CODEX_HOME:-$HOME/.codex}/pets/heidou}"

python3 "$ROOT/scripts/validate_pet.py" "$SRC"

mkdir -p "$DEST"
cp "$SRC/pet.json" "$SRC/spritesheet.webp" "$DEST/"
echo "已安装到 $DEST"
echo "重启 Codex，或在设置里重新选择 pet。"
