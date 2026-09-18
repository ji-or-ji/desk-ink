# 会话交接 — PC 工具

> **面向：PC 工具会话**（opencode）
> **用途：会话上下文被压缩后，从这份恢复。**
> 变更请求回答「要做什么」；这份回答「做到哪了」。
> 更新：2026-09-18

---

## 一、开工前先读

| 文档 | 为什么 |
|---|---|
| `protocol.md` | **`todo` 已改成片版**（`seq`/`total`） |
| `变更-待办-PC端.md` | 待办页面 + 分片发送（未开始） |
| `变更-新闻-PC端.md` | 新闻来源与推送（未开始） |
| `tools/render-preview.ps1` | 合成预览的**参考实现**（同算法、同字库） |
| `software/font/README.md` | 设备字库格式（预览必须用真字库） |

另外 `docs/README.md` 开头有协作约定，五条。

---

## 二、变更请求状态

| 文档 | 状态 |
|---|---|
| `变更-插画-PC端.md` | ✅ **完成**（`ArtworkService` / `MonoBitmap` / `ArtworkPage`）。只有**实际串口上传没测**（没硬件） |
| `变更-PC配置显示与预览.md` | ✅ **完成**（`DeviceConfigService` / `DeviceFont` / `CompositeRenderer` / `MonoCanvas`） |
| `变更-新闻-PC端.md` | ✅ **完成**（`NewsService` + 设置页「新闻」区；RSS/Atom、网址、文件、手动四种源） |
| `变更-待办-PC端.md` | ✅ **完成**（`TodoPage` + `BuildTodoChunks` 分片 + 设备回传重组） |

### 当前界面现状

导航栏：`管理` / `插画` / `一言` / `待办`（设置固定在左下角 footer）。

服务层已有的：`ArtworkService`、`QuoteService`、`NewsService`、`DeviceConfigService`、`DeviceFont`、
`CompositeRenderer`、`MonoCanvas`、`PushCoordinator`、`ClassIslandIpcService` 等。

> 另：启动时把串口枚举/打开、字库读取放到了后台线程（原来在 UI 线程上会「窗口出来但点不动」），
> 并加了单实例锁。

---

## 三、待办那条链的真实状态（重要）

**已改为分片**（`ProtocolBuilder.BuildTodoChunks`，每片 2 条，带 `seq`/`total`）：

```csharp
public const int TodoItemsPerChunk = 2;
public static IReadOnlyList<string> BuildTodoChunks(IEnumerable<TodoItem> items);
```

`PushCoordinator.PushTodo` 逐帧发、片间 20ms；`HandleTodo` 按 `seq`/`total` 重组
（`seq == 0` 清空重来、跳号丢弃），收齐后整体覆盖 `settings.Todos` 并触发 `TodoUpdated`。
实测 8 条 × 14 汉字 = 4 片，最大帧 **132 字节**（< 255）。

`settings.Todos` 现在由「待办」页维护（增/删/勾选，≤8 条、单条 ≤14 汉字）。

---

## 四、硬规则（最容易被压缩吃掉）

1. **连接时不得自动推送配置和待办。**
   用户在设备上刚改的、刚勾的，会被 PC 的存档当场覆盖。
   只有用户主动点「推送」，或那个「随课表推送」开关打开时才推。

2. **待办必须分片**（每片 2 条，片间约 20 ms）。

3. **新闻的 `title` 留空，文本放进 `body`。**
   设备的 `title` 只占一行、最多 7 个汉字，长标题会被切掉；`body` 会自动换行（最多 49 字）。
   `body` 上限 **49 个汉字**，超出截断补「…」。

4. **预览必须用设备真字库**（`font16_simsun.bin` + `ascii8x16.bin`，随工具打包）。
   用系统字体渲染的预览是假的——字宽不同，信息条长度就不同。

5. **值 → 文案要与设备端 `config_value_label()` 一字不差**（如 `25 分`，不是 `25分钟`）。

---

## 五、下一步（按优先级）

1. **硬件到手后实测**：串口上传链路（插画分片、待办分片、schedule/quote/news）
2. 设备端那份 `变更-待办-设备端.md` 落地后，联调「设备勾选 → PC 列表跟着变」
3. 观感细节（配色、托盘图标状态）——仍是待定项

---

## 六、约定

- 契约（`protocol.md` / `ui-spec.md`）由助手维护，**不要自行改**
- 改动**留在工作区即可，不必自己提交** —— 助手会审阅后分组提交
- 改坐标/取值要**三处同步**：`ui.c`、`tools/render-*.ps1`、PC 端预览
