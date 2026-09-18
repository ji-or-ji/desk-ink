# ESP32 端软件架构

> 更新：2026-09-16
> 状态：**草案**，供编码（opencode）参考
> 上游依据：`ui-spec.md`（显示什么）、`protocol.md`（消息协议）、`gpio-map.md`（引脚）

---

## 一、总体架构

```
                    ┌─────────────────────────────┐
   蓝牙 SPP ──UART0─▶│ link：接收 / 解析 / 投递队列  │
                    └──────────────┬──────────────┘
                                   │ 事件队列
   五向开关 ──GPIO──┐                ▼
                 ├──▶┌─────────────────────────────┐
   RTC 唤醒 ─────┘   │ app：状态机 + 页面渲染        │──▶ eink：刷屏
                    └──────────────┬──────────────┘
                                   │
        ┌──────────┬───────────┬───┴──────┬──────────┐
        ▼          ▼           ▼          ▼          ▼
      store      font        rtc        power      input
    (NVS/FS)   (点阵字库)  (DS3231)   (MCP3021)  (五向开关)
```

---

## 二、模块划分

| 模块 | 职责 | 依赖 |
|---|---|---|
| `eink` | 墨水屏 SPI 驱动、整屏/局部刷新、LUT 管理 | SPI2 |
| `ui` | 页面布局与绘制，输出到 framebuffer | font、eink |
| `font` | 点阵字库读取（二分查找）、UTF-8 解码、画字 | 字库 bin |
| `rtc` | DS3231 读写时间、设置闹钟 | I2C |
| `power` | MCP3021 读电压、换算电量 | I2C |
| `input` | 五向导航开关，GPIO0 ADC 分压读方向，短/长按判定 | GPIO/ADC |
| `link` | UART0 收发、JSON 解析、消息分发 | UART0 |
| `store` | NVS 配置 + LittleFS 文件（课表缓存、待办、插画、一言） | NVS/LittleFS |
| `app` | 顶层状态机，串起上面所有模块 | 全部 |

---

## 三、任务与调度

ESP32-C3 是**单核**，任务数要克制。

| 任务 | 优先级 | 职责 |
|---|---|---|
| `app_task` | 5 | 状态机、页面渲染、刷屏（允许阻塞，墨水屏本来就慢） |
| `link_task` | 6 | 独占 UART0 收数据，解析后投递到事件队列（不能丢字节） |

两者用 **FreeRTOS 队列**通信，`app_task` 从队列取事件（消息到达、五向开关动作、唤醒）。

五向开关用**EXT1 唤醒 + 轮询/定时采样**，不必单独开任务。

> 刷屏一次几百毫秒到几秒，期间 `app_task` 阻塞没问题；`link_task` 独立跑，保证不丢数据。

---

## 四、数据流

```
① PC 推送课表
   UART0 → link_task 解析 → 队列 → app_task 更新内存 + 写 store → 刷新课表区

② ESP32 唤醒
   开机/唤醒 → app_task 初始化 → 读 RTC + 电量 + store 缓存
            → 渲染 → 刷新 → 发 req:schedule → 深睡

③ 五向开关动作
   中断 → 队列 → app_task 切换页面/滚动 → 局部刷新

④ 电量低
   app_task 读 MCP3021 → 换算 → 低于阈值时状态栏提示
```

---

## 五、电源与唤醒（关键设计）

深睡是常态，平均功耗目标 ~18μA。

| 唤醒源 | 引脚 | 用途 |
|---|---|---|
| RTC 闹钟 | GPIO7（EXT0） | 定时刷新、切换节次 |
| 五向开关 | GPIO0（EXT1） | 用户交互，拨动即亮屏 |

### 深睡与蓝牙的配合（难点）

深睡时 UART 停止，蓝牙推来的数据会丢。解法是**推拉结合**：

- **推**：PC 在状态变化时推 `schedule`，ESP32 醒着就即时更新（体验好）
- **拉**：ESP32 每次唤醒后主动发 `req:schedule`，把全量状态拉回来（兜底）

这样即使推送全丢，唤醒后也能自愈。**ESP32 不依赖 PC 在线**，没数据就用缓存渲染，墨水屏正好适合显示旧内容。

### 唤醒时序

```
RTC/五向开关 唤醒
  → 初始化外设（I2C / SPI / UART）
  → 读 DS3231 时间、MCP3021 电量
  → 从 store 读课表/待办缓存
  → 按当前时间点选中要显示的内容
  → 刷新（整屏或局部）
  → 发 req:schedule（若链路可用）
  → 配置下一次闹钟 → 深睡
```

---

## 六、存储与分区

### flash 余量估算（4MB）

| 分区 | 大小 | 内容 |
|---|---|---|
| bootloader + 分区表 | ~64 KB | 固定 |
| NVS | 24 KB | 配置项（轮换状态、番茄钟、待办勾选） |
| app | 1.5 MB | 固件 + 代码 |
| LittleFS | **剩余 ~2.4 MB** | 课表缓存、一言库、待办、插画 |

app 内占用估算：

| 项 | 大小 |
|---|---|
| ESP-IDF 基础 | ~800 KB ~ 1 MB |
| 自己的代码 | ~100 KB（估） |
| 字库（`font16_simsun.bin`） | 134 KB |
| ASCII 字库 | 1.5 KB |

**结论：4MB 相当宽裕。** 插画 15 KB/张，LittleFS 的 2.4 MB 能存 150 张以上。待办回传（`protocol.md` 待定项 1）在空间上不构成约束。

> 已落地：自定义分区表 `software/partitions.csv`（nvs 24KB / app 1.5MB / littlefs 2.4375MB）。
> 首次完整构建（含 LittleFS）实测：app bin **384 KB**，占 1.5MB 分区的 25%。

---

## 七、建议目录结构（给 opencode）

```
software/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   └── main.c              ← 模块初始化 + app_start()
├── components/
│   ├── app/                顶层状态机（IDLE/MENU/CONFIG/...）
│   ├── eink/               墨水屏驱动
│   ├── ui/                 页面与绘制
│   ├── rtc/                DS3231
│   ├── power/              MCP3021
│   ├── input/              五向导航开关
│   ├── link/               UART + 协议解析
│   └── store/              NVS + LittleFS
├── font/                   字库（已有）+ tools/
└── assets/                 待传的插画等
```

字库已在 `software/font/`，通过 `target_add_binary_data` 或读到分区均可。

---

## 八、待定

1. **局部刷新的 LUT 管理**：SSD1619AZ 局部刷新需要单独波形，先跑通整屏再攻这块
2. ~~LITTLEFS vs SPIFFS~~ **已定：LittleFS。** 以托管组件 `joltwallet/littlefs`（^1.22.0）接入，分区表见 `software/partitions.csv`；比 SPIFFS 更耐掉电
3. **五向开关 ADC 校准**：C3 的 ADC 有非线性，优先用校准接口或软件标定阈值
4. **app 分区大小**：默认 1.5MB 够，实测后可能回调
5. ~~蓝牙模块波特率~~ 已核实：现行文档统一为 **115200**（`software-notes.md` / `checklist.md` / `bluetooth-arch.md`）；38400 只出现在已废弃的 `各部分引脚分配/`（HC-05 时期）
