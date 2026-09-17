# 桌面墨水屏信息站 — 项目交接说明

> 这份文档写给**编码工具**（opencode 等）。
> 项目的方案设计是在一段对话里完成的，你没有那段上下文，**本文就是你的入口**。
> 读代码之前请先读本文，按第三节的顺序读文档。

---

## 一、这是什么

一块放在课桌上的 4.2 寸墨水屏信息站，基于 ESP32-C3。

显示内容：课表、待办清单、每日一言、新闻、插画。

**设计主线只有一个：省电。** 墨水屏断电保留画面 + 主控深度休眠 + RTC 定时唤醒，平均功耗目标 ~18μA，1000mAh 电池续航约 32 天。

---

## 二、当前状态

| 部分              | 状态                                                |
| --------------- | ------------------------------------------------- |
| 结构 / 原理图 / 引脚分配 | 已完成（三代定稿）                                         |
| PCB 布线          | **未开始**                                           |
| 方案文档            | **已完成**（本次交接的主体）                                  |
| 代码              | 已建 `components/` 骨架 + `app` 状态机（含设备端配置页）；`font`/`ui`/framebuffer/`link`/`store`(NVS+LittleFS) 已实现，`eink`/`rtc`/`power`/`input` 为桩，可编译出 bin |
| 字库              | **已生成**，见 `software/font/`（宋体 16×16，含中文标点，131 KB） |
| 硬件实物            | 待转接板已到货，但墨水屏尚未点亮；其他模块尚未购买                         |

---

## 三、先读什么（按顺序）

| 顺序 | 文档 | 管什么 |
|---|---|---|
| 1 | `docs/desk-eink-info/项目现状与下一步.md` | 全局概览，先看这个 |
| 2 | `docs/desk-eink-info/ui-spec.md` | 屏幕上显示什么、怎么排 |
| 3 | `docs/desk-eink-info/architecture.md` | 固件模块划分、任务划分 |
| 4 | `docs/desk-eink-info/driver-api.md` | 各模块的 C API 签名 |
| 5 | `docs/desk-eink-info/protocol.md` | PC ↔ ESP32 通信协议 |
| 6 | `docs/desk-eink-info/ui-states.md` | 交互状态机 |
| 7 | `docs/desk-eink-info/font-plan.md` + `software/font/README.md` | 字库格式与生成 |

硬件相关：`docs/desk-eink-info/gpio-map.md`、`checklist.md`、`power-arch.md`、`rtc-arch.md`、`eink-arch.md`、`bluetooth-arch.md`、`encoder-arch.md`。

数据来源相关：`docs/desk-eink-info/classisland-link.md`（课表怎么从电脑到设备）。

---

## 四、开发环境

| 项 | 值 |
|---|---|
| 框架 | ESP-IDF **v6.0.1** |
| IDF 路径 | `D:\esp-idf\v6.0.1\esp-idf` |
| 目标芯片 | ESP32-C3（RISC-V） |
| Flash | 4 MB |
| 工程目录 | `software/` |

**构建方式**

- VS Code 里 `Ctrl+Shift+B`（已配好 `software/.vscode/tasks.json`）
- 或命令行：source `C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1` 后 `idf.py build`

> ⚠️ **不要用 CMake Tools 扩展构建。** 它会自动套 ARM 工具包（本机装的是 STM32 那套），破坏 ESP-IDF 工具链。已在 `software/.vscode/settings.json` 里设 `cmake.configureOnOpen: false`。

---

## 五、关键注意点（踩过的坑，别再踩）

1. **MCP3021 的 I2C 地址是 `0x4B`**（订购码 A3T 决定），不是 0x4D。0x4D 属于 A5T 版本。
2. **ESP32-C3 的 strapping 脚是 GPIO2 / GPIO8 / GPIO9**，GPIO0 不是。GPIO9 决定下载模式。
3. **字库必须包含中文标点**（GB2312 的 1~2 区），否则界面里所有「。」「，」都会缺字。
4. **`driver/i2c.h` 旧驱动在 v6.0 已 EOL**，v7.0 会移除。新代码请用 `driver/i2c_master.h`。
5. **UART0（GPIO20/21）被蓝牙模块占用**，日志必须走 USB Serial/JTAG。
6. **控制器用五向导航开关**，`GPIO0` 走 ADC 分压读方向（详见 `ui-states.md` 第二节）。原方案的 SIQ-02FVS3 已弃用。

---

## 六、建议的开工顺序

**阶段一：不依赖硬件，可以先做**

1. 工程骨架：建 `software/components/` 目录，配 CMakeLists
2. `font` 模块：字库加载 + 二分查找 + UTF-8 解码 + 画字
3. `ui` 模块：把页面渲染进 framebuffer（可用 PC 端脚本导出预览图验证效果）
4. `link` 模块：UART 收发 + JSON 解析 + `protocol.md` 里的消息类型
5. `store` 模块：NVS + LittleFS 读写

**阶段二：等硬件到位**

6. `eink` 模块：SSD1619AZ SPI 驱动、整屏刷新
7. `rtc` / `power` / `input` 模块
8. `app` 状态机（`ui-states.md`）
9. 联调，再攻局部刷新

---

## 七、已知未决项（编码时留意）

| 事项 | 现状 |
|---|---|
| 五向开关选型 | 需选「四方向独立引出」的型号，避开矩阵式接法 |
| 原编码器（GPIO9/10） | 已改用五向开关，这两个脚释放 |
| 番茄钟刷新粒度 | 每分钟 vs 每 5 分钟，待实测功耗 |
| 局部刷新 LUT | 第二阶段再做，先跑通整屏 |
| `news` 字段粒度 | 标题 + 摘要够不够，待定 |
| PC 端小程序 | 独立 .NET 工具，走 IPC 读 ClassIsland（见 `classisland-link.md`），本仓库不含它的代码 |

---

## 八、目录速查

```
desk-ink/
├── bord/          立创 EDA 工程（原理图 / PCB）
├── docs/
│   ├── desk-eink-info/   全部方案文档（核心）
│   ├── 结构图/            结构总图
│   └── refs/             ClassIsland 2.1.0.1 接口 XML（备查）
├── shell/         FreeCAD 外壳模型（仅一代）
└── software/      ESP-IDF 工程
    ├── main/           入口（当前只有 I2C 扫描）
    ├── font/           点阵字库 + 生成工具
    └── .vscode/        IDE 配置与构建任务
```
