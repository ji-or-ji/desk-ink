# PC 端工具 — 技术选型

> 更新：2026-09-17
> 状态：**选型已定**，待实现
> 关联：`classisland-link.md`（数据来源）、`protocol.md`（发给设备的消息）

---

## 一、它是什么

一个常驻 Windows 托盘的小工具，只做两件事：

1. 通过 IPC 从 ClassIsland 读课表状态
2. 打包成协议消息，经蓝牙 SPP 发给 ESP32

**不参与显示**（显示在设备上），它只负责数据搬运和配置。

---

## 二、技术栈

| 项 | 选择 | 理由 |
|---|---|---|
| UI 框架 | **WinUI 3**（Windows App SDK） | 现代 Fluent 观感，原生 Win32 |
| 运行时 | **.NET 8** | ClassIsland 的 IPC 库硬性要求 |
| IPC | `ClassIsland.Shared.Ipc` | 官方提供，读课表状态 |
| 串口 | `System.IO.Ports` | 访问蓝牙 SPP 虚拟 COM 口 |
| 托盘 | `H.NotifyIcon.WinUI` | WinUI 3 没有内置托盘 |
| 部署 | **self-contained（未打包）** | 运行时打进应用目录，免装依赖 |

---

## 三、为什么不选别的

| 方案 | 排除原因 |
|---|---|
| Web（Electron / Tauri / WebView） | 明确不要；体积大、启动慢、不够"原生" |
| **WinUI 2**（UWP） | 跑在沙箱里，串口受限，.NET 8 支持有障碍 |
| WPF | 功能上可行，但观感老旧，与"现代 Win"矛盾 |
| WinForms | 更老旧，仅在"只要能跑"时考虑 |

---

## 四、界面结构

### 托盘（常驻，99% 时间的形态）

- 图标反映状态：已连接 / 未连接 ClassIsland / 串口异常
- 右键菜单：打开管理页 / 打开设置 / 退出

### 管理页（主窗口）

- ClassIsland 连接状态
- 当前读到的课表（当前科目 / 下一节）
- 串口状态与端口号
- 推送日志（最近 N 条）

### 设置页

- 串口选择（自动发现 + 手动指定）
- 推送策略（事件触发 / 定时补推）
- 待办是否随课表一起推
- 开机自启

---

## 五、关键技术点

### 5.1 读 ClassIsland（IPC）

细节见 `classisland-link.md`。要点：

- 装 NuGet 包 `ClassIsland.Shared.Ipc`
- 订阅四个事件：上课 / 课间 / 放学 / 时间点变化
- 从 `IPublicLessonsService` 读属性（当前科目、时段、剩余时间、下一节）

### 5.2 蓝牙串口

- VG6328A 与 PC 配对后映射为虚拟 COM 口
- `SerialPort` 打开，115200 8N1
- **端口号会变**（重新配对、换 USB 口都可能变），所以要做「自动发现 + 手动指定」两手准备

### 5.3 托盘

WinUI 3 无内置托盘，用 `H.NotifyIcon.WinUI`。注意两点：

- 未打包模式下图标资源路径要处理好
- 关闭主窗口时**不退出程序**，缩到托盘（这是托盘工具的常见行为，也是用户预期）

### 5.4 部署

选 **self-contained（未打包）**：

- Windows App SDK runtime 打进应用目录
- 换机器直接拷目录就能跑，不用另装 runtime
- 代价：体积大一些（几十 MB），对自用工具无所谓

---

## 六、与设备的链路

```
ClassIsland ──IPC──▶ 本工具 ──SerialPort──▶ 蓝牙 SPP ──▶ VG6328A ──▶ ESP32
```

消息格式见 `protocol.md`。

---

## 七、待定与已定

> 更新：2026-09-17（第一轮实现后）

### 已定

1. **待办推送的入口**：推送开关放**设置页**（`PushTodo`），管理页只读展示。
2. **开机自启**：**注册表 Run 项，不走启动文件夹**（已实现，`AutoStartService`）——
   键 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，值名 `DeskInk.PcTool`，
   值为带引号的 exe 全路径（保存时取 `Environment.ProcessPath`）。
   设置页开关以**注册表为准**（`IsEnabled()` 直接读该值），点保存时写/删。
   注意：路径是保存那一刻的，挪了目录要重新保存一次。
   > 自启目前**会显示主窗口**（和手动启动一样）；要不要改成静默进托盘见下方「仍待定」。
3. **「手动推送测试」按钮**：已做，在管理页右上。
4. **每日一言来源**：默认走**公益源**（一言 hitokoto，`https://v1.hitokoto.cn/`）；设置页可切换
   「手动填写 / 自定义网址 / 本地文件」。每天取一条（跨天自动刷新），可点「立即获取」。
   联网失败时用内置备用文案兜底，不阻塞推送。设置项：`PushQuote` / `QuoteSource` /
   `QuoteUrl` / `QuoteFile` / `Quote`。
5. **插画上传**：已做，规格见 `变更-插画-PC端.md`，界面在新增的「插画」页。
6. **设备配置镜像 + 合成预览**：已做，规格见 `变更-PC配置显示与预览.md`。设置页只读展示设备上报的
   `cfg`（未上报显示「—」）；插画页加「合成模式」下拉（仅插画 / 全屏+实心条 / 反色条 / 反色自适应 /
   小窗模式），用设备真字库（`software/font/*.bin` 随工具打包）合成，出图与 `tools/render-preview.ps1`
   逐像素一致。**PC 只镜像，不写回设备配置。**
   缩放方式默认改为**手动裁剪**（拖动方框选位置、拖角按目标比例缩放），另保留「拉伸 / 留白」；
   裁剪框以归一化坐标随图保存。原文档默认的「Cover 居中裁剪」被手动裁剪取代。

### 仍待定

1. **观感细节**：沿用 Fluent 默认，还是自定义配色
2. **插画抹除**：`protocol.md` 没有删除消息，等固件侧方案定了再补
3. **插画 id 同步**：设备端不回报已有列表，PC 本地自增 id 可能与设备失步（已知限制）
4. **自启是否静默进托盘**：现在自启和手动启动一样会弹主窗口。托盘工具通常自启时只留托盘图标，
   待定：加个「启动时最小化到托盘」开关，还是自启就一律静默。

---

## 八、开工任务（新会话从这里起步）

> 环境已就绪：.NET SDK **8.0.425**、Windows App Runtime 1.1~2.4、Windows SDK 10.0.26100。
> 未装 Visual Studio，走命令行路线（`dotnet build`）。

### 第一步：把项目立起来

- 位置：`desk-ink/pc-tool/`
- 目标框架：`net8.0-windows10.0.19041.0`
- 未打包（unpackaged）：`<WindowsPackageType>None</WindowsPackageType>`
- 需要的 NuGet 包（实际锁定版本）：
  - `Microsoft.WindowsAppSDK` **1.8.260804001**（WinUI 3）
  - `ClassIsland.Shared.Ipc` **2.1.0.1**（读课表）
  - `System.IO.Ports` 8.0.0（串口）
  - `System.Drawing.Common` 9.0.1（插画图像处理）
  - `H.NotifyIcon.WinUI` 2.3.2（托盘）

> **WindowsAppSDK 必须 ≥ 1.7**。1.6 的 `MrtCore.PriGen.targets` 在 `PrepareForRun` 阶段会调用
> VS 的 `Microsoft.Build.Packaging.Pri.Tasks.dll` / `Microsoft.Build.AppxPackage.dll`，
> 没装 VS 时以 `MSB4062` 失败（且 XamlCompiler 会先静默 exit 1）。1.8 的命令行构建干净通过。

> WinUI 3 的项目模板不一定在命令行可用。**能装模板就装**（`dotnet new install` 搜 `WindowsAppSDK` 相关），**装不上就手写** `csproj` + `App.xaml` + `MainWindow.xaml`，不难。
> 本项目即手写；`H.NotifyIcon` 在 WinUI 下的托盘菜单属性是 `ContextFlyout`（不是 WPF 的 `ContextMenu`）。

### 第二步：跑通 IPC

- 连 ClassIsland，取得 `IPublicLessonsService`
- 把当前科目、下一节读到界面/日志上
- 接口定义见 `classisland-link.md`

### 第三步：跑通串口

- 枚举本机 COM 口，列出候选
- 打开并发送一条测试消息（格式见 `protocol.md`）

### 第四步：串起来

- 订阅四个事件（上课/课间/放学/时间点变化）
- 事件触发 → 组包 → 写串口
- 同时提供托盘与三个界面

### 参考文档

| 文件 | 作用 |
|---|---|
| `pc-tool.md`（本文） | 技术选型与界面结构 |
| `classisland-link.md` | IPC 接口与数据模型 |
| `protocol.md` | 发给 ESP32 的消息格式 |
| `变更-插画-PC端.md` | 插画上传（图像预处理 + 分片）规格 |
| `轮换与插画.md` | 小窗内容池与插画显示模式 |
| `docs/refs/ClassIsland.Shared.IPC-2.1.0.1.xml` | 接口原文（备查） |
