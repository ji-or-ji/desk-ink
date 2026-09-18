# 设计变更请求 — PC 端新增「待办」页面

> 日期：2026-09-18
> **面向：PC 工具会话**（opencode）　**只改 `pc-tool/`，不碰 `software/`**
> 配套：设备端的分片重组与勾选见 `变更-待办-设备端.md`（另一个会话）
> 协议：`protocol.md` 第三节第 4 条**已更新为分片版**，请重读

---

## 一、为什么

PC 端**能推待办，却没有让你输入待办的地方**：

- `ProtocolBuilder.BuildTodo` 有
- `PushCoordinator.PushTodo` 有
- 设置页只有一个「随课表推送待办」开关
- 但 `settings.Todos` 是个空集合，**没有任何界面能填它**

而且导航栏里插画一页、一言一页，**待办没有位置**——结构上就是缺的一块。

待办是核心功能之一，该有自己的页面。

---

## 二、新页面

`Pages/TodoPage.xaml`，**照 `QuotePage` 的模子**（左侧编辑区 + 右侧预览，卡片样式沿用 `CardStyle`）。

导航栏在「一言」之后加一项：

```xml
<NavigationViewItem Content="待办" Tag="todo">
    <NavigationViewItem.Icon>
        <FontIcon Glyph="&#xE9D5;" />
    </NavigationViewItem.Icon>
</NavigationViewItem>
```

`MainWindow.xaml.cs` 的 `Nav_SelectionChanged` 里加对应分支。

---

## 三、功能范围（只做这些）

| 做 | 不做 |
|---|---|
| 增（输入框 + 回车/按钮） | 拖拽排序 |
| 删（每行一个删除按钮） | 分组、标签、优先级 |
| 勾选（复选框） | 截止日期、提醒 |
| 条数与长度上限提示 | 自动清除已完成 |

**上限（写进界面提示，超了就拦住）：**

- 最多 **8 条**（设备待办区放得下）
- 每条 ≤ **14 个汉字**（设备待办区一行宽 236px，`x=30` 起）

超出时给出明确提示，**不要静默截断**。

---

## 四、发送：必须分片

**这是本次最容易漏的一点。** 现在 `BuildTodo` 返回**一条**消息：

```csharp
public static string BuildTodo(IEnumerable<TodoItem> items)
    => JsonSerializer.Serialize(new TodoMessage { Items = items.Take(8).ToList() }, Options);
```

单条 JSON 的固定开销就有 30 字节（`{"id":1,"done":true,"text":""}`）：

- **8 条即使文本全空也已 269 字节** > 255 上限
- 装 14 个汉字约 **590 字节**，2.3 倍

所以必须改成**分片发送**：

```json
{"t":"todo","seq":0,"total":2,"items":[{...},{...}]}
{"t":"todo","seq":1,"total":2,"items":[{...}]}
```

| 项 | 值 |
|---|---|
| 每片条数 | **2 条**（2×72 + 外壳 ≈ 186 字节 < 255） |
| `total` | `ceil(count / 2)` |
| 片间间隔 | 约 20 ms（别灌爆蓝牙缓冲） |

**建议**：`BuildTodoChunks(items) → IReadOnlyList<string>`，返回多帧；`PushCoordinator.PushTodo` 改为逐帧发。

---

## 五、接收设备回传

设备勾选后会回传**分片**的 `todo`（全量语义）。PC 要：

1. 按 `seq` / `total` **累加重组**（`seq == 0` 清空重来，跳号丢弃）
2. 收齐后**整体覆盖** `settings.Todos`
3. 刷新页面列表

> 页面上正在编辑时收到回传，直接覆盖即可——条目少，不做合并。

---

## 六、⚠️ 硬规则：连接时不得自动推送

```
PC 连上 → 设备 hello → PC 推 schedule（这个照旧）
                      → 但**不推** todo
```

理由和配置同步那条一样：用户在设备上刚勾的、刚改的，会**当场被 PC 的存档覆盖**。

所以：**连接时不推待办**。只有用户主动点「推送」，或那个「随课表推送」开关打开时才推。

---

## 七、不改的地方

- **`software/**` 一个字都不改**
- 已有的 `PushTodoSwitch` 保留
- 插画页 / 一言页 / 设置页

---

## 八、验收

1. 导航栏出现「待办」，点进去是编辑区 + 预览
2. 能增、能删、能勾选；超过 8 条或单条超 14 字会拦住并提示
3. 点推送 → 串口出现多条 `todo`，带 `seq`/`total`，**每帧 < 255 字节**
4. 推 8 条中文，设备那边一条不漏
5. 设备上勾选 → PC 页面**列表跟着变**
6. PC 重连后**不会**自动把待办推下去

---

## 九、参考

- `pc-tool/Pages/QuotePage.xaml` —— **直接对标这个文件的布局与样式**
- `pc-tool/Services/ProtocolBuilder.cs` —— `BuildTodo` 现在的位置
- `pc-tool/Services/PushCoordinator.cs` —— `PushTodo` 现在的位置
- `docs/desk-eink-info/protocol.md` 第三节第 4 条（**已更新**）
