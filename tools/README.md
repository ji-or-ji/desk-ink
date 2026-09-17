# tools — 离线预览工具

## `render-preview.ps1`

把一张图片压成墨水屏的 **1-bit 画面**，可选叠加信息条，输出放大后的 PNG。

**它不只是"看图"用的**——这就是 PC 端「插画上传」预处理流程的**参考实现**：
缩放/裁剪 → 灰度 → 有序抖动(4×4 Bayer) → 1-bit 打包 →（可选）信息条叠加。
`pc-tool/Services/ArtworkService.cs` 实现时可直接对照。

### 为什么可信

- 文字用的是**设备端的真实字库**（`software/font/font16_simsun.bin` + `ascii8x16.bin`），
  所以预览里的字和屏上一致
- 抖动、1-bit 打包的约定与固件、协议完全对齐（见下）

### 用法

```powershell
# 不传源图 → 用内置合成场景（亮天空 + 山 + 地面）
.\render-preview.ps1 -Bar solid -Out bar-solid.png

# 真实图片
.\render-preview.ps1 -Source D:\pic.jpg -Bar auto -Out preview.png

# 小窗尺寸、关抖动、反色
.\render-preview.ps1 -Source D:\pic.jpg -Width 134 -Height 200 -NoDither -Invert
```

### 参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `-Source` | 空 | 源图片路径；留空用内置合成场景 |
| `-Width` / `-Height` | 400 / 300 | 目标尺寸（小窗用 134 / 200） |
| `-Fit` | `cover` | `cover` 居中裁剪 / `stretch` 拉伸 / `contain` 留白 |
| `-NoDither` | 关 | 关掉抖动，改用固定阈值 |
| `-Threshold` | 128 | 阈值（仅 `-NoDither` 时生效） |
| `-Invert` | 关 | 整体反色 |
| `-Bar` | `none` | `none` / `solid` 实心黑条 / `invert` 反色整条 / `auto` 反色自适应 |
| `-BarHeight` | 32 | 信息条高度 |
| `-BarLeft` / `-BarRight` | `09:40` / `第3节 数学` | 信息条两端的文字 |
| `-Out` | `preview.png` | 输出路径 |
| `-Scale` | 2 | 放大倍数（像素级放大，便于看细节） |

### 三种信息条模式的实测差异

| 模式 | 行为 | 实测（真实照片当底） |
|---|---|---|
| `solid` | 条内填黑，画白字 | 永远可读 |
| `invert` | 整条取反，画白字 | **看运气**：照片顶部偏暗时，取反后偏亮，白字消失 |
| `auto` | 逐段取反，按取反后的明暗自动选字色 | 可读；但两段可能一段白字一段黑字，观感会跳 |

> `auto` 的实现与 `docs/desk-eink-info/变更-显示侧.md` 第五节给的伪代码一致，
> 已实测通过。固件侧按同一逻辑实现即可。

### 约定（与固件 / 协议一致，别改）

| 项 | 值 |
|---|---|
| 1-bit 排列 | 行优先，**MSB 在左** |
| 取值 | **1 = 黑** |
| 行跨度 | `ceil(width / 8)` 字节 |
| 总长度 | 400×300 → 15000 字节；134×200 → 3400 字节 |
| 抖动矩阵 | 4×4 Bayer，判定 `灰度 < (矩阵值 + 0.5) / 16 × 255` |

> ⚠️ 面板 RAM 的极性是反的（1 = 白），刷屏那一层要取反。见 `software-notes.md`。

---

## `render-states.ps1`

把固件 `ui.c` 的**各个界面状态**画成一张对照图（默认 4×3 共 12 格）。

用途：不用读代码、也不用硬件，一眼看清**已经实现到什么程度**。

```powershell
.\render-states.ps1 -Photo D:\pic.jpg -Out states.png
```

它覆盖的状态：

| # | 状态 |
|---|---|
| ① ② | IDLE · 每日一言 / 新闻 |
| ③ | IDLE · 插画（小窗） |
| ④ ⑤ ⑥ | IDLE · 全屏插画 · 信息条（实心 / 反色 / 自适应） |
| ⑦ | IDLE · 内容池为空 |
| ⑧ | MENU |
| ⑨ | CONFIG（反白值块） |
| ⑩ ⑪ | TODO_NAV / TODO_EDIT |
| ⑫ | POMO |

**这是 ui.c 的忠实移植**：布局常量、绘制原语（`fb_*`）、字模绘制、`ui_render_*` 的
坐标与文案，全部照 `software/components/ui/ui.c` 抄；取数与 `app.c` 的
`seed_placeholder_data()` 一致。

> 注意边界：它是**移植**，不是编译产物。用来对「实现了几分」够用；
> 要验证真代码，走 `docs/desk-eink-info/qemu-验证.md` 那条路。
