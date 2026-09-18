# Bad Apple demo：PC 工具链 + 固件播放器

> 更新：2026-09-19
> 状态：**PC 工具链与固件骨架已完成，QEMU 逻辑验证通过；局部刷新 LUT 与实测帧率待硬件**
> 上游：`会话交接-BadApple.md`

---

## 一、目标

把局部刷新压到极限的压力测试，不是做视频播放器。验收：从 PC 连续播放 30 秒，画面能认出是 Bad Apple，不卡死、不花屏。

---

## 二、组成

| 部分 | 位置 | 说明 |
|---|---|---|
| PC 转换工具 | `tools/badapple/` | 视频 -> 抽帧 -> Bayer 抖动 -> 1-bit -> PackBits RLE -> BAF1 帧包 |
| 帧格式 | `tools/badapple/README.md` | BAF1 容器 + PackBits，PC 与固件共用 |
| 播放器 | `software/components/demo/` | 读 BAF1 -> 解压 -> 写 framebuffer -> 刷新 |
| 帧来源接口 | `software/components/demo/include/demo_source.h` | 只抽一层间接，当前只有文件源 |
| 局部刷新 | `software/components/eink/eink.c` | `eink_flush_partial` 已实现几何与数据通路 |
| demo 入口 | `software/components/app/` | Kconfig `CONFIG_APP_DEMO_BADAPP`，不改既有状态机 |

数据流：

```
视频 --ffmpeg--> 灰度帧 --Bayer--> 1-bit --PackBits--> BAF1
       （PC，tools/badapple）                              |
                                                          v
                            LittleFS 上的 badapple.baf（文件源）
                                                          |
                                                          v
        demo 播放器 --PackBits 解码--> framebuffer --局部刷新--> 面板
```

---

## 三、`eink_flush_partial`

几何与数据通路按 SSD1619AZ 的通用序列实现，窗口约定与 `eink_flush_full` 对齐：

- 面板 RAM 的 X 地址以字节（8 像素）为单位，窗口向外取整。
- 0x45 的 Y 是「起点 高 终点 高」四字节。因为数据入口模式 `0x11=0x01` 下 Y 计数递减（整屏那套就是从 299 写到 0），所以窗口起点取面板坐标的 `y1-1`、终点取 `y0`，整屏时正好回到 `299/0`，与现有 `eink_flush_full` 完全一致。
- 只把该矩形的字节取反后写入 0x24，再 `0x22` + `0x20` 触发并等 BUSY。

**待实测**：`EINK_PARTIAL_UPDATE_PARAM`（默认 `0xCF`）是占位值。仓库里没有规格书 LUT 表，局部刷新到底用哪套波形、是否需要先用 `0x32` 写独立 LUT，必须等硬件到位后实测确认。见 `会话交接-BadApple.md` 第二节。

---

## 四、QEMU 验证

QEMU 不模拟墨水屏 SPI，`CONFIG_APP_SKIP_EINK=y` 时 `eink_init` 不执行，两个 flush 都返回 `ESP_ERR_NOT_SUPPORTED`，但解码与 blit 照跑，正好用来验逻辑。

构建（独立 build 目录与 sdkconfig，别污染主构建）：

```powershell
. 'C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1'
$env:PATH = 'C:\Program Files\Hanako\resources\git\mingw64\bin;' + $env:PATH
Set-Location software
idf.py -B build-qemu-badapple -D SDKCONFIG=sdkconfig.qemu.badapple `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu.defaults;sdkconfig.badapple.defaults" build
```

`CONFIG_DEMO_EMBED_BUNDLE=y` 会把 `software/testdata/badapple.baf` 编进固件，启动时写入 LittleFS；`CONFIG_DEMO_MAX_FRAMES` / `CONFIG_DEMO_DUMP_EVERY` 控制播几帧、每几帧 dump 一次。合并 flash 与运行照 `qemu-验证.md` 第四节，把串口日志存成文件后：

```powershell
pwsh -File tools\fb64-decode.ps1 -Log <日志> -OutDir <输出目录> -Scale 2
```

### 验证结果（2026-09-19）

- demo 跑完 8 帧，日志 `Bad Apple done (ESP_OK)`。
- 把 QEMU dump 的 8 帧 framebuffer 与 PC 工具对同一份 `badapple.baf` 的解码结果**逐字节比对，8/8 完全一致**。这同时验证了 BAF1 索引/帧头解析、固件端 PackBits 解码、以及 blit 后的 framebuffer 布局。
- 普通构建（`APP_DEMO_BADAPP=n`，`APP_QEMU_TOUR=y`）同样编译通过，未回归。

---

## 五、实测数据

| 项 | 值 | 说明 |
|---|---|---|
| 单帧裸数据 | 15000 字节 | 400x300 1bpp |
| 合成高对比度片段压缩比 | 约 13.4x | 24 帧，单帧载荷约 1100 字节 |
| 合成测试图（渐变）压缩比 | 约 2.5x | 中间调抖动后退化，属预期 |
| 真实 Bad Apple 压缩比 | **未测** | 仓库里没有视频源，等真片 |

按 13x 估算，3 分半 2fps 约 420 帧、裸数据 6.3MB，压缩后约 0.5MB，放得下 LittleFS。

---

## 六、待硬件

| 项 | 说明 |
|---|---|
| 局部刷新 LUT / 波形 | `EINK_PARTIAL_UPDATE_PARAM` 与是否写独立 LUT，需实测 |
| 窗口 Y 方向与计数约定 | 逻辑上已与整屏对齐，仍需点亮确认方向正确 |
| 实际帧率、残影、连续刷稳定性 | 局部刷推算 0.3 到 0.5 秒/帧，需实测 |
| 30 秒真片验收 | 需要真实 Bad Apple 视频 |
| PC 串口推流 | 当前只有文件源；实时流协议留到硬件阶段 |

---

## 七、踩到的坑

- **ffmpeg 不带 libx264**：本机那份是 lgpl-shared，合成测试视频要用 `mpeg4` 之类可用编码器。
- **Python 输出编码**：Windows 管道下默认 GBK，工具里已 `reconfigure(utf-8)`，日志与重定向统一 UTF-8。
- **长时间构建会被会话工具杀掉**：构建改成后台进程跑，再轮询日志。
