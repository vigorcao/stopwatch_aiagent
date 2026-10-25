# 黑豆技术架构

本文说明黑豆（heidou）这个 Codex pet 的结构、制作流程和加载方式。

**术语**

- **pet 包**：Codex 读取的一个目录，含 `pet.json` 和 `spritesheet.webp`。
- **图集**（atlas）：一张大图，把所有动画帧排成网格。
- **格子**（cell）：图集里的一帧，大小 192×208 像素。
- **动作行**：图集的一行，对应一个动作，例如 `idle`。
- **视线方向**：黑豆朝向某个角度看的姿势。共 16 个，每 22.5° 一个。

## 1. 总览

```
参考图 heidou-original.png
        │  $imagegen（hatch-pet 流程）
        ▼
基础图 canonical-base.png ──▶ 9 个标准动作行 ──▶ 4 个基准视线 ──▶ 2 个视线行
        │                         │                    │               │
        └─────── 切帧、去青色背景、逐行 QA ────────────┴───────────────┘
                                  ▼
                  8×11 图集 spritesheet.webp (1536×2288)
                                  ▼
                 pet/heidou/{pet.json, spritesheet.webp}
                                  ▼  scripts/install.sh
                  ~/.codex/pets/heidou/  ──▶ Codex 读取并显示
```

## 2. pet 包

Codex 从下面的位置读取自定义 pet：

```
${CODEX_HOME:-$HOME/.codex}/pets/<pet-id>/
├── pet.json
└── spritesheet.webp
```

黑豆的 [pet.json](../pet/heidou/pet.json)：

```json
{
  "id": "heidou",
  "displayName": "黑豆",
  "description": "黑豆是一只热情、调皮的黑色毛茸茸小怪兽。",
  "spriteVersionNumber": 2,
  "spritesheetPath": "spritesheet.webp"
}
```

`spriteVersionNumber` 必须是 `2`。缺省值是 1。按 hatch-pet 的规格文档，版本 1 会拒绝 2288 像素高的图集。Codex 根据这个字段得到 11 行布局和视线行为。

## 3. 图集布局

- 尺寸：1536×2288，8 列 × 11 行，格子 192×208。
- 格式：WebP，透明背景。
- 标准动作行里，用到的最后一列之后的格子必须全透明。

| 行 | 动作 | 帧 | 用途 |
|---:|---|---:|---|
| 0 | `idle` | 6 | 呼吸、眨眼的待机循环 |
| 1 | `running-right` | 8 | 向右拖动 |
| 2 | `running-left` | 8 | 向左拖动 |
| 3 | `waving` | 4 | 打招呼 |
| 4 | `jumping` | 5 | 悬停或跳跃 |
| 5 | `failed` | 8 | 失败、被拦截、被取消 |
| 6 | `waiting` | 6 | 等待批准或输入 |
| 7 | `running` | 6 | 任务执行中 |
| 8 | `review` | 6 | 结果就绪，等待检查 |
| 9 | 视线 A | 8 | 000°、022.5°、045°、067.5°、090°、112.5°、135°、157.5° |
| 10 | 视线 B | 8 | 180°、202.5°、225°、247.5°、270°、292.5°、315°、337.5° |

**特殊格子：第 0 行第 6 列**是视线的中立（正面）帧。`idle` 本身只有 6 帧（列 0–5）。这一格有内容是正常的，`scripts/validate_pet.py` 已按此处理。

**视线方向的约定**

- 角度顺时针增加。
- `000` 表示朝上（12 点钟），不是正面。
- 鼠标指针靠近中心的死区时，Codex 回退到 `idle`。

**帧时长**（来自 hatch-pet 的 `animation-rows.md`）

| 动作 | 时长（毫秒） |
|---|---|
| `idle` | 280, 110, 110, 140, 140, 320 |
| `running-right` / `running-left` | 每帧 120，末帧 220 |
| `waving` / `jumping` | 每帧 140，末帧 280 |
| `failed` | 每帧 140，末帧 240 |
| `waiting` | 每帧 150，末帧 260 |
| `running` | 每帧 120，末帧 220 |
| `review` | 每帧 150，末帧 280 |

## 4. 制作流程

黑豆在 2026-09-25 用 hatch-pet 流程制作。这个流程把"生成图像"和"确定性处理"分开。

**生成图像**（`$imagegen`，每个任务一个独立 worker）共 13 个任务：

1. 基础图：以 [heidou-original.png](../assets/reference/heidou-original.png) 为参考，生成 [canonical-base.png](../assets/reference/canonical-base.png)。后续所有任务都以它为身份基准。
2. 9 个标准动作行。每行附带版式参考图（只管帧数和留白，不进成品）。
3. 1 个基准视线条带：上、右、下、左 4 个姿势。
4. 2 个视线行：先做行 9，通过 QA 后再做行 10。

**确定性处理**（Python，不调用生成模型）：

1. 按帧切分每一行，检查边缘裁切和连通域。
2. 去掉青色（`#00FFFF`）抠像背景。
3. 边缘去色溢（despill），只改边缘像素的 RGB，不改透明度。
4. 拼成 8×11 图集。
5. 校验尺寸、透明度、残留色边。

**黑豆的视线机制**（完整说明见 [look-mechanics.md](../qa/look-mechanics.md)）

- 双脚和下半身保持同一基线。
- 上半身和脸部轻微弯曲、转向，双臂小幅跟随。
- 眼球是立体结构。转向时，高光、橙色虹膜、黑色瞳孔和眼睑一起变化。
- 不旋转或倾斜整只身体。

## 5. 质量检查

所有结果都在 [qa/](../qa) 目录。

| 检查 | 文件 | 结果 |
|---|---|---|
| 图集结构、透明度、色边 | [atlas-validation.json](../qa/atlas-validation.json) | `ok: true`，错误和警告均为空 |
| 色边清理 | [chroma-despill-extended.json](../qa/chroma-despill-extended.json) | `ok: true`，透明度保持不变，0 个像素被拒绝 |
| 4 个基准视线 | [cardinal-anchors.json](../qa/cardinal-anchors.json) | `ok: true`，无错误 |
| 视线方向语义 | [direction-semantics.json](../qa/direction-semantics.json) | `ok: true`，16 个方向 |
| 视线盲测 | [direction-blind-validation.json](../qa/direction-blind-validation.json) | `ok: true`，无错误，4 条警告 |
| 视线连续性 | [look-continuity.json](../qa/look-continuity.json) | `ok: true`，18 条警告 |
| 逐帧检查 | [review.json](../qa/review.json) | `ok: true` |
| 独立视觉复核 | [final-visual-qa.json](../qa/final-visual-qa.json) | `pass` |

**警告说明**

- 连续性的 18 条警告里，16 条是"内部透明洞"。位置都在 y≈173–192，是双腿之间的空隙。独立复核认定这是自然空隙，不是抠像缺陷。
- 连续性的另外 2 条是差异离群：157.5°→180° 和 270°→292.5°。复核认为没有可见的尺寸跳变。
- 盲测的 4 条警告说明水平轴或垂直轴有歧义，涉及 157.5°、202.5°、247.5°、292.5°。这几个方向靠近正上或正下，小尺寸下左右感较弱。复核人员在正常大小下逐格确认了方向。

## 6. 目录与文件对应

| 路径 | 内容 |
|---|---|
| `pet/heidou/` | 可安装的 pet 包 |
| `assets/reference/` | 原始参考图、基础图 |
| `assets/preview/` | 图集总览、视线总览、9 个动作 GIF |
| `build/pet_request.json` | 制作参数：图集规格、抠像色、角色描述、风格约束 |
| `qa/` | 全部 QA 报告 |
| `scripts/install.sh` | 校验后安装到 Codex |
| `scripts/validate_pet.py` | 检查 pet 包是否符合 v2 规格 |
| `scripts/slice_spritesheet.py` | 把图集切成 74 张单帧透明 PNG |

## 7. 范围与限制

- 本仓库保存**成品和检查记录**，不含当时的 `$imagegen` 提示词和中间条带。生成步骤因此不能按字节重现。
- `build/pet_request.json` 和 `qa/*.json` 里的本机路径已替换为 `<run>`、`<out>`。
- 黑豆在 M5StopWatch 上的加载、固件和烧录见 [stopwatch.md](stopwatch.md)。手表用的是修复版图集 [pet/heidou-repaired/](../pet/heidou-repaired)。
