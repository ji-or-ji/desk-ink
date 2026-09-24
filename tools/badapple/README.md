# tools/badapple — Bad Apple 帧序列转换工具

把视频转成墨水屏固件可以直接播放的帧包。这是 Bad Apple demo 的 PC 端工具链，固件播放器见 `software/components/demo/`（尚未实现）。

## 它做什么

```
视频 -> ffmpeg抽帧(灰度8bit,400x300,cover裁剪)
     -> 4x4 Bayer有序抖动 -> 1-bit打包(1=黑, MSB在左)
     -> PackBits RLE压缩 -> .baf容器
     -> 附带 manifest.json 与若干预览PNG
```

抖动与打包的约定跟 `tools/render-preview.ps1`、固件 `eink.c` 完全一致：行优先、每行 `ceil(width/8)` 字节、MSB在左、1=黑、0=白，判定式是 `灰度 < (Bayer值+0.5)/16*255`。400x300 时一帧裸数据是 `50*300=15000` 字节。

## 依赖

- Python3（本机 `C:\Espressif\tools\python\python.exe`，3.11），只用标准库，不需要 Pillow。
- ffmpeg 在 PATH 里，或者用 `--ffmpeg` 指定路径。抽帧靠 ffmpeg 直接吐原始灰度字节，所以不依赖任何图像库。

## 用法

```powershell
# 转整段视频，默认 400x300、2fps，输出到 out/badapple.baf
python tools\badapple\badapple.py convert D:\badapple.mp4 --fps 2

# 只取第 30 秒起的 30 秒（验收要的 30 秒片段）
python tools\badapple\badapple.py convert D:\badapple.mp4 --fps 2 --start 30 --duration 30

# 先小样试跑：只转 20 帧，出 4 张预览
python tools\badapple\badapple.py convert D:\badapple.mp4 --fps 2 --limit 20 --previews 4

# 把帧包还原成 PNG，肉眼跟原片比对
python tools\badapple\badapple.py decode --in out\badapple.baf --every 10 --scale 2

# 看头部与压缩统计
python tools\badapple\badapple.py info --in out\badapple.baf

# 不依赖 ffmpeg 的内部往返自测
python tools\badapple\badapple.py selftest
```

`convert` 的主要参数：

| 参数 | 默认 | 说明 |
|---|---|---|
| `video` | 必填 | 输入视频 |
| `--fps` | 必填 | 目标帧率 |
| `--width` / `--height` | 400 / 300 | 目标尺寸，`cover` 语义居中裁剪 |
| `--start` / `--duration` | 0 / 不限 | 起始秒、截取秒数 |
| `--limit` | 0 | 最多转多少帧，0 表示不限 |
| `--out` | `out/badapple.baf` | 输出帧包 |
| `--previews` | 6 | 均匀导出几张预览PNG |
| `--no-dither` / `--threshold` | 关 / 128 | 关抖动改固定阈值，仅供调试 |
| `--ffmpeg` | `ffmpeg` | ffmpeg路径 |

## BAF1 容器格式

全部小端。位图原始长度是 `ceil(width/8)*height`，行优先、MSB在左、1=黑。

头部固定 16 字节：

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | char[4] | magic | `"BAF1"` |
| 4 | u8 | version | 1 |
| 5 | u8 | rle_id | 1 = PackBits |
| 6 | u8 | flags | 保留，当前 0 |
| 7 | u8 | reserved | 0 |
| 8 | u16 | width | 400 |
| 10 | u16 | height | 300 |
| 12 | u16 | fps_milli | 帧率x1000 |
| 14 | u16 | frame_count | 帧数 |

接着是 `frame_count` 条索引，每条 8 字节：`u32 offset`（帧记录的绝对文件偏移）、`u32 length`（帧记录总长，含帧头）。

每条帧记录：

| 类型 | 字段 | 说明 |
|---|---|---|
| u16 x4 | dirty_x, dirty_y, dirty_w, dirty_h | 相对上一帧的变化区域，首帧是整屏；x 对齐到字节边界；两帧相同则为 0,0,0,0 |
| u8[] | payload | PackBits 压缩后的位图 |

## PackBits RLE

控制字节 `c`：

| c | 含义 |
|---|---|
| 0..127 | 后接 `c+1` 个原样字节 |
| 129..255 | 后接 1 个字节，重复 `257-c` 次 |
| 128 | 空操作 |

固件端解码用 `int8_t c = (int8_t)buf[i]`：`c >= 0` 时拷贝 `c+1` 个字节，`c < 0` 时重复 `1-c` 次（`c == -128` 跳过）。这个方案对大面积纯黑纯白（整行 `0xFF`/`0x00`）压缩极高，对抖动后的中间调退化成原样存储，最坏情况接近 1x，不会膨胀。

## 已经验证的

- `selftest` 覆盖 PackBits 往返、抖动优化与逐像素参考实现一致、dirty 区域包围盒、容器读写，64 项全过。
- 用 ffmpeg 合成视频端到端跑过 `convert -> info -> decode`，20 帧重新抽帧再抖动后与帧包解码结果逐字节比对，20/20 一致。
- 合成高对比度片段的实测压缩比约 13x（24 帧，单帧载荷约 1100 字节）。真实 Bad Apple 的压缩比要拿真片测，目前仓库里没有视频源。

## 边界与后续

- 只做落盘，不做串口推流。推流要等固件端播放协议定下来（USB串口，二进制分片，不走已有 JSON 协议）。
- 不做帧间差分压缩。交接文档明确说先跑通再优化。dirty 区域只是元数据，压缩仍是逐帧独立。
- 首帧固定按整屏记录 dirty 区域，播放器从第一帧开始就能全屏刷新一次。
