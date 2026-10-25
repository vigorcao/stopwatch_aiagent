# 黑豆在 M5StopWatch 上：加载与烧录

本文说明黑豆怎么从 Codex pet 变成手表上的动画，以及怎么编译、烧录。

**术语**

- **手表**：M5Stack StopWatch。ESP32-S3，466×466 圆形 AMOLED，按键 A/B，USB 端口通常是 `/dev/cu.usbmodem1101`。
- **小助手**：Mac 上的 `voicecode` 程序（[mac/voicecode.swift](../mac/voicecode.swift)）。它通过蓝牙和手表通信。
- **帧图**：手表能直接显示的单帧图片，LVGL `.bin` 格式。
- **storage 分区**：手表闪存里 4MB 的 FAT 分区，挂载在 `/spiflash`。帧图和中文字库都放在这里。

## 1. 总览

```
按住 A 说话 → 手表录音 → 蓝牙（16 kHz IMA-ADPCM，64 kbps）→ Mac 小助手
   → 本地 Whisper 转文字 → 打进当前窗口（ChatGPT / Codex）并回车

Codex 干活 → ~/.codex/hooks.json 调 mac/voicecode-hook.sh <状态> gpt
   → 小助手 → 蓝牙 → 手表 GPT 应用 → PetView 播放黑豆对应的动画
```

手表只做三件事：收音、发蓝牙、显示状态。转文字、打字、运行 Codex 都在 Mac 上。

交互演示见 [demo/stopwatch-pet.html](../demo/stopwatch-pet.html)。

## 2. 从 Codex pet 到手表帧图

```
pet/heidou-repaired/spritesheet.webp (1536×2288, 8×11 格)
        │  tools/make_pet_frames.py  缩放 0.8、压 LZ4、透明度压到背景色
        ▼
firmware/fs/pets/heidou/<动作>_<序号>.bin   (65 个文件，约 1.1MB)
        │  wl_fatfsgen.py 打包
        ▼
build/storage.bin ──esptool──▶ 手表 0xa00000（storage 分区）
        │
        ▼
PetView 运行时读 /spiflash/pets/heidou/*.bin
```

**为什么用修复版图集**：原图集 `running-right` 行的 8 帧是同一个姿势，双腿不交替。在手表上，黑豆入场时会"滑"进屏幕。[pet/heidou-repaired/](../pet/heidou-repaired) 用 hatch-pet 修复模式只重画了第 1、2 行（running-right、running-left）。其余行和 `pet/heidou/` 逐像素一致。修复提示词见 [REPAIR_PROMPT.md](../pet/heidou-repaired/REPAIR_PROMPT.md)。

**make_pet_frames.py 做的事**（[tools/make_pet_frames.py](../tools/make_pet_frames.py)）：

1. 按 Codex v2 规格切出 8 个动作：`idle`、`walk`（来自 running-right）、`waving`、`jumping`、`failed`、`waiting`、`running`、`review`。
2. 按 `scale` 缩放。黑豆用 0.8，帧大小 154×166。
3. 把透明度压到背景色上，得到 RGB565（每像素 2 字节）。背景色必须和应用背景一致，黑豆是 `F0ECE2`。
4. 用 LVGL LZ4 压缩（`--lz4`）。比 `--rle` 小约 40%。

## 3. 固件里的黑豆

固件在 [firmware/](../firmware)。它基于 M5Stack 官方的 [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)（ESP-IDF 5.5.4，MIT 许可）。

| 文件 | 作用 |
|---|---|
| [main/apps/app_voice_code/app_voice_code.cpp](../firmware/main/apps/app_voice_code/app_voice_code.cpp) | `AppVoiceCode::gpt()` 创建 GPT 应用，用 `PetView("heidou", false, 0xF0ECE2, true)` |
| [main/apps/app_voice_code/view/pet_view.cpp](../firmware/main/apps/app_voice_code/view/pet_view.cpp) | 读帧图、按状态选动画、走入场、按倾斜看方向 |
| [main/hal/hal_voice_link.cpp](../firmware/main/hal/hal_voice_link.cpp) | 蓝牙链路，接收小助手推来的状态 |
| [main/hal/utils/voice_code_protocol.h](../firmware/main/hal/utils/voice_code_protocol.h) | 手表和小助手之间的协议，状态写入带上 agent（claude / gpt / openclaw） |
| [partitions.csv](../firmware/partitions.csv) | 分区表，`storage` 是 4MB FAT |
| [sdkconfig.defaults](../firmware/sdkconfig.defaults) | 打开 LZ4 解码、`LV_BIN_DECODER_RAM_LOAD` 等 |

**状态和动画的对应**（`pet_view.cpp` 的 `clip_for`）：

| 手表状态 | 触发 | 黑豆动画 |
|---|---|---|
| Idle | 无任务 | `idle` |
| Listening | 按住 A | `waving` |
| Sending | 松开 A，转文字中 | `running` |
| Thinking | Codex 思考 | `review` |
| Working | Codex 执行工具 | `running` |
| Permission | Codex 请求权限 | `waiting` |
| Done | 任务完成 | `jumping` |
| Error | 出错 | `failed` |

**其他行为**

- **入场**：打开 GPT 应用时，黑豆用 `walk_0..7` 从左边走进来。1.5 秒后停在正中。
- **看方向**：待机时手表平放或竖直，黑豆看正面。手表倾斜超过 30°，黑豆看向低的一侧。倾斜回到 20° 以内，恢复正面。
- **摇一摇**：陀螺仪超过 400°/s 时，待机的黑豆会跳一下。

## 4. 编译与烧录

**前提**

- ESP-IDF 5.5.4，安装在 `~/esp/esp-idf`。
- Python 环境里有 Pillow 和 `lz4`（`pip install pillow lz4`）。

**步骤**

1. 生成黑豆帧图（只在换图集时需要。仓库里已有生成好的 `firmware/fs/pets/heidou`）：
   ```bash
   python tools/make_pet_frames.py pet/heidou-repaired firmware/fs/pets/heidou 0.8 --lz4
   ```
2. 编译并只烧应用。这一步保留设置和蓝牙配对：
   ```bash
   cd firmware
   source ~/esp/esp-idf/export.sh
   rm -f sdkconfig        # 改过 sdkconfig.defaults 后必须删
   idf.py build
   idf.py -p /dev/cu.usbmodem1101 -b 921600 app-flash
   ```
3. 打包并烧录 storage 分区。会覆盖分区里原有的图片：
   ```bash
   rm -rf build/fs_device && mkdir -p build/fs_device/pets
   cp -R fs/fonts build/fs_device/            # 中文字库，生成方法见下
   cp -R fs/pets/heidou build/fs_device/pets/
   python $IDF_PATH/components/fatfs/wl_fatfsgen.py build/fs_device --output_file build/storage.bin \
     --partition_size 0x400000 --sector_size 4096 --long_name_support
   esptool --chip esp32s3 -p /dev/cu.usbmodem1101 write-flash 0xa00000 build/storage.bin
   ```

**中文字库**：仓库不含 `fs/fonts/cjk20.bin`。用下面的命令生成（字体是阿里巴巴普惠体 Medium，20px，4bpp，覆盖 GB2312）：

```bash
npx -y lv_font_conv@1.5.2 --font PuHuiTi-Medium.ttf -r 0x20-0x7E \
  --symbols "$(cat cjk_symbols.txt)" --size 20 --bpp 4 --format bin --no-compress \
  -o firmware/fs/fonts/cjk20.bin
```

`cjk_symbols.txt` 是 GB2312 全部汉字和符号。

**storage 容量**：4MB 装不下所有宠物。真机只装字库（约 0.8MB）+ 黑豆（约 1.1MB）+ 煤球 3D（约 1.6MB）。本仓库只带黑豆的帧图。

## 5. 网页模拟器

固件的 `main/apps` 可以编译成 WebAssembly，在浏览器里运行：

```bash
cd firmware
emcmake cmake -S web -B web/build -G Ninja      # 只需首次
cmake --build web/build
python3 ../tools/serve_web.py                  # 打开 http://localhost:8765/stopwatch.html
```

- 鼠标 = 触屏。`A` / `←` = 按键 A。`B` / `→` = 按键 B。`H` = 长按 A+B 回主页。
- 模拟器预加载 `fs/pets` 里的帧图。只改了帧图时，先删 `web/build/stopwatch.html` 和 `web/build/stopwatch.data` 再编译。
- 用 `serve_web.py` 而不是 `python3 -m http.server`。前者发 `Cache-Control: no-store`，浏览器不会用旧文件。

## 6. Mac 端

| 文件 | 作用 |
|---|---|
| [mac/voicecode.swift](../mac/voicecode.swift) | 小助手：收手表音频、Whisper 转文字、打字、转发状态 |
| [mac/voicecode-hook.sh](../mac/voicecode-hook.sh) | Claude Code / Codex 的 hook，把状态发到 `/tmp/voicecode.sock` |
| [mac/voicecode_status.swift](../mac/voicecode_status.swift) | 手动推状态的命令行工具 |
| [mac/ble_probe.swift](../mac/ble_probe.swift)、[mac/hid_watch.swift](../mac/hid_watch.swift) | 蓝牙调试工具 |

编译：`swiftc -O mac/voicecode.swift -o mac/voicecode`。

Codex 侧的配置：在 `~/.codex/hooks.json` 里，让各个事件调用 `voicecode-hook.sh <状态> gpt`。

## 7. 踩过的坑

- LVGL 从文件流式解码缩放的 RGB565 `.bin` 会花屏（竖条纹）。需要 `LV_BIN_DECODER_RAM_LOAD=1`。
- 改了 `sdkconfig.defaults` 后要先 `rm sdkconfig`。已有 `sdkconfig` 里写着 `# CONFIG_xxx is not set` 的选项不会被覆盖。
- `lv_image_set_scale` 绕图片中心缩放，不改变对象尺寸。贴底边对齐时要补偿 (scale−1)×h/2。
- macOS 只在加密绑定的链路上启用 BLE 键盘。需要 `CONFIG_BT_NIMBLE_SM_LVL=2` 和 `CONFIG_BT_NIMBLE_NVS_PERSIST`。
- pyserial 打开端口时默认拉高 DTR/RTS，会让芯片进入下载模式。读日志用 [tools/serial_log.py](../tools/serial_log.py)。
- 读串口的进程被强行杀掉后，芯片可能停在下载模式（屏幕黑）。用看门狗复位：
  `esptool --chip esp32s3 -p /dev/cu.usbmodem1101 --after watchdog-reset chip-id`。

## 8. 范围

- `firmware/` 是 2026-10-25 的工作区快照：`dev` 分支（最新提交 `d159010`）加上未提交的改动。它也包含和黑豆无关的应用（Aura、Timer、Tamago、Meiqiu）。
- `fs/` 只带黑豆的帧图。字库和其他宠物不在仓库里。
- `mac/` 只有源码，没有编译好的二进制。
