# PC ↔ ESP32 消息协议（草案）

> 更新：2026-09-16
> 状态：**草案**，字段定完后才进编码
> 上游依据：`ui-spec.md`（界面要显示什么）、`classisland-link.md`（数据源有什么）

---

## 一、传输层

| 项 | 值 |
|---|---|
| 物理链路 | PC（蓝牙 SPP）↔ VG6328A ↔ UART0 ↔ ESP32-C3 |
| 串口参数 | 115200 8N1（VG6328A 侧，见 `software-notes.md`） |
| 方向 | 双向 |
| 单帧上限 | **255 字节**（VG6328A 限制），超长必须分片 |

注意：ESP32 的 UART0 被蓝牙模块独占，**日志走 USB Serial/JTAG，不混进这条链路**。

---

## 二、帧格式

采用 **JSON Lines**：一条消息一行，以 `\n` 结尾。

```
{"t":"schedule", ...}\n
{"t":"quote", ...}\n
```

- 逐行读取，行内不允许出现裸换行
- 行长度 ≤ 255 字节
- ESP32 端**自写轻量解析**（消息结构固定）。注：ESP-IDF v6.0.1 已移除内置 cJSON 组件，改用 `link_json.c` 的取值式解析，比引 cJSON 更省
- 类型字段用 `"t"`（解析时兼容 `"type"`）

---

## 三、PC → ESP32

### 1. `schedule` — 课表状态（核心）

```json
{"t":"schedule","state":1,
 "cur":{"name":"数学","initial":"数","start":"09:40","end":"10:20","left":23},
 "next":{"name":"英语","start":"10:30","end":"11:10"}}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `state` | int | 0=无 1=上课 2=准备上课 3=课间 4=放学（对应 ClassIsland 的 `TimeState`） |
| `cur.name` | string | 当前科目名；课间时为课间名称（如「课间休息」） |
| `cur.initial` | string | 单字简称，界面窄时可只显示它 |
| `cur.start` / `cur.end` | string | `HH:MM` |
| `cur.left` | int | 剩余分钟。上课中=距下课，课间中=距上课 |
| `next.name` | string | 下一节课科目 |
| `next.start` / `next.end` | string | `HH:MM` |

约 150 字节，一帧装得下。

### 2. `news` — 新闻

```json
{"t":"news","title":"标题","body":"正文摘要"}
```

### 3. `quote` — 每日一言

```json
{"t":"quote","text":"把一件事做完整，比做十件半成品更有价值。"}
```

### 4. `todo` — 待办同步（双向）

PC 推送中文待办，或 ESP32 回传本地改动，两个方向用同一类型。

```json
{"t":"todo","items":[
  {"id":1,"done":true, "text":"英语单词 Unit5"},
  {"id":2,"done":false,"text":"买电池"},
  {"id":3,"done":false,"text":"预习生物第 3 章"}
]}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `items[].id` | int | 条目 ID，两端对齐用 |
| `items[].done` | bool | 是否勾选 |
| `items[].text` | string | 条目文本，**可含中文** |

- 谁发就表示「这是我的当前全量」，收到后**整体覆盖**本地，不做逐条合并（条目少，简单优先）
- ESP32 本地编辑只支持英文（见 `ui-spec.md`），**中文条目由 PC 推送**
- 条目上限建议 8 条（待办区能放下），超出时 PC 端先截断

> flash 余量已估算（LittleFS ~2.4MB），不构成约束，详见 `architecture.md`。

### 5. 插画上传（分片）

插画是 400×300 单色位图，约 15 KB，远超单帧上限，走分片：

```json
{"t":"img_begin","id":3,"name":"cat","bytes":15000,"total":60}
{"t":"img_chunk","id":3,"seq":0,"data":"<base64>"}
...
{"t":"img_end","id":3,"crc":12345}
```

- 每片 base64 载荷控制在 **180 字节以内**（保证整帧 < 255）
- ESP32 收到 `img_begin` 后按 `id` 建缓冲，`img_end` 校验 CRC 后落盘
- 中途收到新 `img_begin` 则丢弃旧任务
- **CRC 定义：CRC16-CCITT，多项式 `0x1021`，初值 `0xFFFF`，不反转**（对拼装后的完整位图字节计算）

> 插画由 PC 端小程序上传/抹除，ESP32 本地存储并轮换显示（见 `ui-spec.md`）。

---

## 四、ESP32 → PC

| 类型 | 示例 | 用途 |
|---|---|---|
| `hello` | `{"t":"hello","fw":"0.1.0"}` | 上电握手，PC 收到后全量推一次 |
| `ack` | `{"t":"ack","for":"schedule"}` | 确认收到某类消息 |
| `req` | `{"t":"req","what":"schedule"}` | 主动请求刷新（如五向开关切换、唤醒后） |
| `status` | `{"t":"status","bat":3.85}` | 状态上报（电池电压等） |

---

## 五、交互时序

```
ESP32 上电 ──hello──▶ PC
PC ──schedule──▶ ESP32          （首次全量）
PC ──quote────▶ ESP32

上课/下课/时间点变化（ClassIsland 事件）
   PC ──schedule──▶ ESP32       （状态变化时推）

ESP32 从深睡唤醒
   ESP32 ──req:schedule──▶ PC
   PC ──schedule──▶ ESP32

五向开关切到下一屏
   ESP32 本地切换（不联网）
```

PC 端订阅 ClassIsland 的 `OnClassNotifyId` / `OnBreakingTimeNotifyId` / `OnAfterSchoolNotifyId` / `CurrentTimeStateChangedNotifyId`，事件触发就推一次 `schedule`。

---

## 六、错误与超时

| 情况 | 处理 |
|---|---|
| JSON 解析失败 | 丢弃该行，继续读下一行 |
| 分片序号跳号 | 丢弃整个图片任务，等重新 `img_begin` |
| `img_end` CRC 不匹配 | 丢弃，PC 择机重传 |
| 长时间无 `schedule` 更新 | ESP32 显示保持上一次内容（墨水屏天然适合） |
| PC 未连接 | ESP32 正常显示缓存数据，不阻塞 |

---

## 七、待定

1. ~~待办是否回传~~ **已定：做。** PC 可推送中文待办，ESP32 可回传本地改动，消息定义见第三节 `todo`。flash 余量不构成约束。
2. **插画分片的 base64 vs 二进制**：base64 省事但膨胀 33%，若实测太慢再换二进制分片
3. **`news` 的字段粒度**：标题 + 摘要够不够，是否要来源、时间
4. ~~ESP32 端 JSON 库选型~~ **已定：自写轻量解析。** ESP-IDF v6.0.1 已无内置 cJSON，见 `link_json.c`
5. **心跳机制**：是否需要周期性心跳判断链路存活
6. **配置项是否需要 PC 端可改**：目前设计为纯本地（见 `ui-states.md` 附节），若以后想在电脑上改，再扩协议
