# 设计变更请求 — PC 端新增「插画上传」

> 日期：2026-09-17
> **面向：PC 工具会话**（opencode）。只改 `pc-tool/`。
> 背景：你已完成 PC 工具的骨架（`App.xaml`、`MainWindow`、两个页面、十个 Service）。
> 本次新增一个功能，**只动 PC 端（`pc-tool/`）**，固件不用改。

---

## 一、变更概述

PC 端新增「插画」功能，链路是：

```
导入本地图片 → 简单编辑（缩放/裁剪/二值化/抖动/反色）→ 预览 → 转 1-bit → 分片上传
```

**为什么只动 PC 端**：固件端的分片接收已经完成（`link.c` 里 `img_begin` / `img_chunk` / `img_end` + `crc16_ccitt` 齐全），落盘也好了（`app.c` 的 `MSG_IMAGE` 分支）。**缺的只是发送端。**

> 注：设备端「把插画画到屏幕上」还没实现，那是另一条线，不在本次范围内。

---

## 二、目标尺寸（重要：两种都要支持）

插画的尺寸由设备配置项 `cfg_art_mode` 决定，PC 端**两种都做**，由界面上的选项切换，**默认 400×300**：

| 模式 | 尺寸 | 用途 |
|---|---|---|
| 全屏 | **400×300** | 铺满整屏（配信息条叠加） |
| 小窗 | **134×200** | 放进右列小窗 |

在导入对话框里选，选择结果记进本地设置，下次默认沿用。

---

## 三、要改的地方

### 1. `Models/Protocol.cs` — 新增三个消息类

```csharp
public sealed class ImgBeginMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "img_begin";
    [JsonPropertyName("id")] public int Id { get; set; }
    [JsonPropertyName("name")] public string Name { get; set; } = string.Empty;
    [JsonPropertyName("bytes")] public int Bytes { get; set; }
    [JsonPropertyName("total")] public int Total { get; set; }
}

public sealed class ImgChunkMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "img_chunk";
    [JsonPropertyName("id")] public int Id { get; set; }
    [JsonPropertyName("seq")] public int Seq { get; set; }
    [JsonPropertyName("data")] public string Data { get; set; } = string.Empty;
}

public sealed class ImgEndMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "img_end";
    [JsonPropertyName("id")] public int Id { get; set; }
    [JsonPropertyName("crc")] public int Crc { get; set; }
}
```

### 2. `Services/ProtocolBuilder.cs` — 新增三个构帧方法

```csharp
public static string BuildImgBegin(int id, string name, int bytes, int total);
public static string BuildImgChunk(int id, int seq, string base64);
public static string BuildImgEnd(int id, int crc);
```

沿用现有 `Options`（`UnsafeRelaxedJsonEscaping`，否则 base64 里的字符会被转义）。

### 3. `Services/ArtworkService.cs` — **新增**，图像处理 + 上传

职责分两半：

**（a）图像处理**：`Bitmap → 1-bit 位图字节`

```csharp
public sealed record ArtworkOptions(
    int TargetWidth,          // 400 或 134
    int TargetHeight,         // 300 或 200
    ArtworkFit Fit,           // Stretch | Cover(居中裁剪) | Contain(留白)
    bool UseDither,           // 抖动开关
    int Threshold,            // 0..255，仅在不抖动时生效
    bool Invert);

public static byte[] ToMonoBitmap(Bitmap source, ArtworkOptions options);
```

**（b）分片上传**：

```csharp
public Task UploadAsync(byte[] mono, int id, string name, CancellationToken ct);
```

### 4. `Pages/ArtworkPage.xaml` — **新增**页面

- 左侧：插画列表（缩略图 + 名称 + 尺寸）
- 右侧：预览（按 1:1 显示处理后的 1-bit 效果，放大 2× 便于看）
- 顶部：导入按钮、目标尺寸选择
- 参数区：缩放方式 / 抖动 / 阈值 / 反色，改动即时刷新预览
- 底部：上传按钮 + 进度条（第 n / N 片）+ 取消

在 `MainWindow` 的导航里加一项（现有是「管理」「设置」两个页面）。

---

## 四、规格细节（照这个来，别自创）

### 4.1 1-bit 打包格式

| 项 | 约定 |
|---|---|
| 排列 | 行优先，从上到下、每行从左到右 |
| 位序 | **MSB 在左**（一行内第 0 个像素占最高位） |
| 取值 | **1 = 黑点（涂墨）**，0 = 白 |
| 行跨度 | `stride = ceil(width / 8)` 字节 |
| 总长 | `stride × height` 字节 |

例：400×300 → stride 50 → 15000 字节；134×200 → stride 17 → 3400 字节。

> ⚠️ 这个「1 = 黑」的约定，固件端画屏时必须一致（或自行取反）。对不上就整屏反相，请跟固件那边对齐。

### 4.2 分片

| 项 | 值 |
|---|---|
| 每片原始数据 | **128 字节** |
| base64 后 | 172 字符 |
| 单帧长度 | ≈ 215 字节（含 JSON 外壳与换行）< 255 ✅ |
| 片数 | `ceil(总字节 / 128)`，15000 字节 → 118 片 |
| 片间间隔 | 每片之间 `await Task.Delay(20)`，避免灌爆蓝牙 SMB 缓冲 |

**CRC**：CRC16-CCITT，多项式 `0x1021`，初值 `0xFFFF`，不反转（`refIn=false, refOut=false, xorOut=0`），**对完整位图字节计算**，不是逐片。

### 4.3 图像处理默认参数

| 项 | 默认 |
|---|---|
| 缩放方式 | Cover（居中裁剪后缩放，不变形） |
| 抖动 | 开（4×4 有序抖动，Bayer 矩阵） |
| 阈值 | 128（抖动关闭时才用） |
| 反色 | 关 |

**有序抖动矩阵**（4×4，取值 0..15）：

```
 0  8  2 10
12  4 14  6
 3 11  1  9
15  7 13  5
```

判定：`像素灰度 < (矩阵值 + 0.5) / 16 × 255` 则为黑。

---

## 五、不需要改的地方

- `components/**`（固件全部不动）
- `SerialLinkService`（用现成的 `SendLine`）
- `ClassIslandIpcService` / `PushCoordinator` / `QuoteService` / `SettingsService`
- `protocol.md`（三段式早定义好了，本次只是把它落地）

---

## 六、注意

1. **上传期间不要并发第二张**。别的消息（schedule 等）照常发不影响，但 `img_begin` 会顶掉旧任务（固件侧行为），所以要串行。
2. **`id` 由 PC 端分配**，自增即可，本地记一份「id → 名称」映射。设备端目前不会回报已有插画列表，**两端可能失步**——这是已知限制，先接受。
3. **抹除功能本次不做**。协议里没定义删除消息，等固件侧方案定了再补。
4. 图像处理建议用 **`System.Drawing.Common`**（同步、简单；Windows 专用，本场景没问题）。若不想加 NuGet 依赖，可用 `Windows.Graphics.Imaging`，但代码会啰嗦不少。
5. 大图导入前先 `Dispose` 掉旧的 `Bitmap`，避免句柄泄漏。

---

## 七、参考

- `docs/desk-eink-info/protocol.md` 第三节第 5 条（插画上传分片，含 CRC 定义）
- `docs/desk-eink-info/轮换与插画.md`（显示模式与内容池，**本次只取其中的「PC 端预处理」部分**）
- `docs/desk-eink-info/pc-tool.md`（技术选型）

---

## 八、验收标准

1. 导入一张 1024×1024 的 jpg，选「全屏」，能预览出 400×300 的 1-bit 效果
2. 改抖动 / 阈值 / 反色，预览即时变化
3. 点上传，进度条走到 100%，串口日志能看到 `img_begin` → 118 片 `img_chunk` → `img_end`
4. 串口日志里每一帧都不超过 255 字节
