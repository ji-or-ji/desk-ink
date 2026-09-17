# 课表数据链路 — ClassIsland 侧调研

> 更新：2026-09-16
> 目的：确定「课表数据怎么从电脑走到 ESP32」，为编码铺路
> 资料来源：ClassIsland **2.1.0.1** 官方发行包（full folder 版）。
> 数据模型与 IPC 接口直接从包内的 `ClassIsland.Shared.IPC.xml` / `ClassIsland.Shared.xml` 读取，不是反编译。

---

## 一、链路总览

```
ClassIsland（教室/家里的电脑，C# / .NET）
      ↕  IPC（进程间通信，ClassIsland.Shared.Ipc）
PC 端小程序（C# / .NET 8）
      ↕  SerialPort 写入蓝牙 SPP 虚拟 COM 口
VG6328A（经典蓝牙，透传）
      ↕  UART
ESP32-C3
```

**结论：不用写 ClassIsland 插件。** 走 IPC，PC 端就是个独立小工具。

---

## 二、ClassIsland 数据模型（2.1.0.1 确认）

### Subject（科目）

| 字段 | 类型 | 说明 |
|---|---|---|
| `Name` | string | 科目名，如「数学」 |
| `Initial` | string | 简称，未填时自动取 `Name` 首字，如「数」 |
| `TeacherName` | string | 教师名 |
| `IsOutDoor` | bool | 是否户外课（体育、信息技术） |

内置常量：`Subject.Fallback`（2.1 起的空科目）、`Subject.Empty`（`?` / `???`）、`Subject.Breaking`（`休` / `课间休息`）

> `Initial` 对墨水屏很有用：400px 宽，显示简称比全名舒服得多。

### TimeLayoutItem（时间点）

| 字段 | 类型 | 说明 |
|---|---|---|
| `StartSecond` | DateTime | 开始时刻 |
| `EndSecond` | DateTime | 结束时刻 |
| `StartTime` / `EndTime` | — | 2.1 新增的更直接的时间属性 |
| `Last` | TimeSpan | 时长（只读，`End - Start`） |
| `TimeType` | int | 0=上课、1=课间、2=分割线、3=行动 |
| `BreakName` | string | 自定义课间名称（`BreakNameText` 取实际值） |
| `DefaultClassId` | string | 默认科目 ID |

### TimeLayout（时间表）

| 字段 | 类型 | 说明 |
|---|---|---|
| `Name` | string | 时间表名称 |
| `Layouts` | 集合 | `TimeLayoutItem` 列表 |

### TimeState（状态枚举）

`None` / `OnClass`（上课）/ `PrepareOnClass`（准备上课）/ `Breaking`（课间）/ `AfterSchool`（放学）

---

## 三、IPC 接口

NuGet 包 `ClassIsland.Shared.Ipc`，要求 .NET 8+ 或 .NET Framework 4.7.2+。

### IPublicLessonsService（核心服务）

| 成员 | 类型 | 说明 |
|---|---|---|
| `IsTimerRunning` | bool | 主计时器是否在跑 |
| `CurrentClassPlan` | ClassPlan? | 当前加载的课表 |
| `CurrentSelectedIndex` | int | 当前时间点索引，无则 -1 |
| **`CurrentSubject`** | Subject? | 当前科目（课间时为课间名） |
| **`CurrentTimeLayoutItem`** | TimeLayoutItem | 当前时间点 |
| **`NextClassSubject`** | Subject | 当前或下一节课的科目 |
| **`NextClassTimeLayoutItem`** | TimeLayoutItem | 当前或下一个上课时间点 |
| `NextBreakingTimeLayoutItem` | TimeLayoutItem | 下一个课间时间点 |
| **`OnBreakingTimeLeftTime`** | TimeSpan | 距下课剩余（上课中） |
| **`OnClassLeftTime`** | TimeSpan | 距上课剩余（课间中） |
| `CurrentState` | TimeState | 当前状态 |
| `IsClassPlanEnabled` / `IsClassPlanLoaded` / `IsLessonConfirmed` | bool | 状态标志 |
| `MultiWeekRotation` | 集合 | 多周轮换 |
| `GetClassPlanByDate(DateTime)` | ClassPlan? | 按日期取课表 |

> 界面上要显示的东西，这里全都有：当前科目、时段（`CurrentTimeLayoutItem` 的 Start/End）、时长（`Last`）、剩余（两个 LeftTime）、下节课及其时段。

### 事件 ID（IpcRoutedNotifyIds）

| 常量 | 实际值 |
|---|---|
| `OnClassNotifyId` | `classisland.lessonsService.onClass` |
| `OnBreakingTimeNotifyId` | `classisland.lessonsService.onBreakingTime` |
| `OnAfterSchoolNotifyId` | `classisland.lessonsService.onAfterSchool` |
| `CurrentTimeStateChangedNotifyId` | `classisland.lessonsService.currentTimeStateChanged` |

### 用法示意（C#）

```csharp
var client = new IpcClient();
await client.Connect();
var lessons = client.Provider.CreateIpcProxy<IPublicLessonsService>(client.PeerProxy);

client.JsonIpcProvider.AddNotifyHandler(
    IpcRoutedNotifyIds.OnClassNotifyId,
    () => { /* 上课了 */ });
```

---

## 四、界面字段 ↔ 数据来源对照

| 界面元素 | 数据来源 |
|---|---|
| 当前课程名 | `CurrentSubject.Name`（或 `.Initial`） |
| 当前节次 | `CurrentSelectedIndex` |
| 时段 xx:xx–xx:xx | `CurrentTimeLayoutItem.StartSecond` / `EndSecond` |
| 总时长 | `CurrentTimeLayoutItem.Last` |
| 剩余时间 | 上课中取 `OnBreakingTimeLeftTime`，课间中取 `OnClassLeftTime` |
| 下节课 | `NextClassSubject.Name` |
| 下节时段 | `NextClassTimeLayoutItem.StartSecond` / `EndSecond` |
| 状态 | `CurrentState` |

---

## 五、PC 端小程序职责

1. 启动后 IPC 连接 ClassIsland，连不上就重试
2. 拉取一次当前课表状态
3. 订阅上课 / 课间 / 放学 / 时间点变化事件
4. 事件触发或定时到达时，把状态打包成 JSON，经串口发出
5. 常驻托盘，保持轻量

---

## 六、传输与消息格式

- Windows 上 VG6328A 配对后映射成虚拟 COM 口，.NET 用 `SerialPort` 直接写
- 消息带类型头：

```json
{"t": "schedule", "state": "onClass", ...}
{"t": "news",  ...}
{"t": "quote", ...}
```

- 单包 ≤ 255 字节，超长分包（VG6328A 限制）

> 字段级定义另开 `protocol.md`。

---

## 七、待确认

1. 蓝牙 SPP 在 Windows 上的配对流程与 COM 口稳定性
2. 哪台机器负责跑课表版 ClassIsland（本机这台只当灵动岛用，没排课表）
3. `ClassIsland.Shared.IPC` 的 NuGet 包版本需与目标 ClassIsland 版本对齐（当前确认基于 2.1.0.1）
