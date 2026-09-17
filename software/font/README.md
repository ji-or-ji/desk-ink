# 字库（software/font）

墨水屏显示用的点阵字库，以及生成它们的工具。

## 文件

| 文件 | 内容 | 大小 |
|---|---|---|
| `font16_simsun.bin` | 3943 个字符 = GB2312 1~2 区符号（188）+ 一级汉字（3755），16×16 点阵 | 131 KB |
| `ascii8x16.bin` | 96 个 ASCII 字符（0x20~0x7F），8×16 点阵 | 1.5 KB |

## 二进制格式

### font16_simsun.bin

| 偏移 | 内容 |
|---|---|
| 0x00 | magic `F16B`（4 字节） |
| 0x04 | uint16 字符数 N |
| 0x06 | uint16 codes[N]，码点升序，供二分查找 |
| 0x06 + 2N | uint8 bitmaps[N][32]，每字 16 行 × 2 字节 |

- 每字 32 字节，行内 MSB 在左（bit15 = 最左像素）
- 取字流程：UTF-8 → 码点 → 二分查 `codes` → 下标 × 32 → 点阵位置

### ascii8x16.bin

- 96 字符 × 16 字节，按 0x20~0x7F 顺序排列
- 每字节一行 8 像素，MSB 在左

## 重新生成

```powershell
# 汉字字库（1~2 区符号 + 16~55 区一级汉字）
pwsh -File tools/build_font.ps1

# ASCII 字库
pwsh -File tools/build_ascii.ps1

# 渲染界面预览，输出到 docs/desk-eink-info/ui-preview.png
pwsh -File tools/render_preview.ps1
```

脚本用系统字体 `C:\Windows\Fonts\simsun.ttc` 在 PC 端取模，路径基于脚本自身位置推算，不写死盘符。

## 关键参数（改之前先看这段）

| 项 | 取值 | 原因 |
|---|---|---|
| 字体 | 宋体 SimSun | 16px 附近笔画最锐，横竖卡在像素网格上 |
| 字号 | 16px | 18px 会溢出 16×16 网格，17px 与 16px 几乎无差别 |
| 取模方式 | `TextRenderer`（GDI）+ 单色 | 走 GDI 通道，无抗锯齿，接近墨水屏实际观感 |
| 排版标志 | `TextFormatFlags.NoPadding` | 默认边距会让字形整体右移约 2px |

## 注意

**中文标点必须包含。** 句号、逗号、顿号、间隔号这些不在汉字区，而在 GB2312 的 1~2 区。漏掉的话界面上所有「。」「，」「·」都会变成空白。
