# 设计变更请求 — QEMU 状态巡视（把 UI 验证从硬件挪走）

> 日期：2026-09-18
> **面向：硬件 / 固件会话**（opencode）
> 目的：**让板子到手后只剩接线和时序**，界面渲染不再占周末时间。

---

## 一、现状

QEMU 已经能跑到 `app started`，但只看得到**开机那一次**渲染，而且 `dump_fb_ascii(100, 30)`
是 100×30 的粗采样——**看不出小字对不对、信息条压没压住插画**。

而界面上真正的风险恰好都在细节里：

- 信息条三种模式压在插画上可不可读
- 小窗正文换行有没有把词拆开
- 配置页六项的值块位置对不对

这些现在只能靠硬件点屏，或者靠助手的**复刻**（`tools/render-*.ps1`，不是真代码）。

---

## 二、要加两个 Kconfig（都默认 `n`）

### 1. `APP_QEMU_TOUR` —— 开机自动走一遍所有状态

打开后，`app_main` 在首帧渲染之后，依次驱动并**每步 dump 一次 framebuffer**：

| 顺序 | 状态 |
|---|---|
| 1 | `IDLE`（小窗=每日一言） |
| 2 | `IDLE`（小窗=新闻） |
| 3 | `MENU`（光标停在「配置」） |
| 4 | `CONFIG`（六项各停一次） |
| 5 | `TODO_NAV`（光标第 2 条） |
| 6 | `TODO_EDIT` |
| 7 | `POMO`（初始时长） |
| 8 | 全屏插画 + 信息条 **实心** |
| 9 | 全屏插画 + 信息条 **反色** |
| 10 | 全屏插画 + 信息条 **反色自适应** |
| 11 | 插画进小窗（`art_mode=1`） |

每步之间留 ~200 ms，让日志来得及刷出去。

**数据**直接用 `seed_placeholder_data()` 那套（第3节 数学、三条待办、2026-09-17 周四），
**插画**：若 LittleFS 里没有图，就在内存里造一张合成图（渐变天空 + 山），保证全屏模式可看。

> 这一步只驱动**渲染路径**，不走 `input_poll`，所以 `APP_SKIP_INPUT` 照样开着。

### 2. `APP_QEMU_DUMP_B64` —— 把 framebuffer 精确打出来

用 base64 打整帧：

```
15000 字节 → base64 20000 字符 → 每行 200 字符 → 约 100 行
```

格式建议：

```
FB64 BEGIN 15000
<200 字符 × N 行>
FB64 END
```

主机端脚本把 `BEGIN`/`END` 之间拼起来解码，就是**精确的 1-bit 位图**，直接转 PNG。

> 为什么用 base64：hex 要 30000 字符，base64 省 1/3。115200 baud 下 20 KB 约 2 秒，无所谓。

---

## 三、为什么要做

做完之后，**界面这块在板子到之前就验证完了**，而且是**真 C 代码**渲染的：

- 三种信息条模式在真实合成图上的可读性
- 换行禁则有没有生效
- 配置页六项排版
- 插画全屏/小窗的落位

板子到手就只剩：**接线对不对、时序对不对、屏亮不亮**。

---

## 四、不改的地方

- 正常运行：两个开关默认 `n`，**行为完全不变**
- `APP_SKIP_INPUT` / `APP_SKIP_EINK` 保留（巡视时仍需要）
- 不动 `ui.c` 的渲染逻辑——巡视只是**调用它**

---

## 五、验收

1. 带 `APP_QEMU_TOUR=y` + `APP_QEMU_DUMP_B64=y` 构建，在 QEMU 里跑
2. 日志里出现 11 段 `FB64 BEGIN/END`
3. 主机端脚本能还原出 11 张 400×300 的 1-bit 图
4. 图里：汉字清晰、信息条位置正确、配置页值块居中——**与 `tools/render-states.ps1` 的对应格一致**
5. 默认配置下构建运行，行为与现在**完全一样**

---

## 六、参考

- `qemu-验证.md` —— QEMU 跑通的配方与四个坑
- `tools/render-states.ps1` —— 期望结果的参照（复刻版）
- `docs/desk-eink-info/点灯清单.md` —— 这份做完之后，清单第四节以后才是周末的活

---

## 七、实现提示（来自一次未完成的尝试，供参考）

- **只调 `ui.h` 的公开 API**，不要依赖 `app.c` 的内部状态（`s_state` 等）。
  可用：`ui_set_schedule/todo/date/clock`、`ui_set_art_mode/bar_mode`、
  `ui_pool_clear/set_text/set_images/set_current`、`ui_render_idle/menu/config/todo_nav/todo_edit/pomo`。
  这样巡视是**独立文件**，不侵入 app.c。
- **挂载点**：`app_start()` 里 `dump_fb_ascii(100,30)` 之后、`xTaskCreate(app_task, ...)` **之前**
  —— 巡视跑完才起 app_task，不会并发重绘。
  用 `#if CONFIG_APP_QEMU_TOUR` 包一个 `extern void qemu_tour_run(void);` + 调用即可，不必改 `app.h`。
- **插画**：不要直接往 framebuffer 画，而是**把合成的 400×300 / 134×200 位图写进 LittleFS**
  （`store_write_file("img_1.bin", ...)`），再 `ui_pool_set_images()`
  —— 这样走的是真实的「读文件 → 判长度 → blit」路径。
- **base64 输出**：每行 200 字符，末尾 `fflush(stdout)`；BEGIN/END 各带标签，主机好切块。
- **构建**：`idf.py qemu` 会重建 flash 镜像，**必须带上同一个 `-D SDKCONFIG=...`**，
  否则 root 那份 sdkconfig 会盖回来。
- 注意：CLI 传参时别把 idf.py 的 `-B` 直接放在 python 后面（`python -B` 是 python 自己的旗标）。

---

## 八、待澄清（已澄清）

先前复跑停在 `eFuse: calibration ...` 之后、`app_main` 之前，**不是 LittleFS 慢**，
而是 `input_init()` 里 `adc_oneshot_new_unit()` 的 ADC 自校准——QEMU 不模拟该外设，死循环。
根 `sdkconfig` 被 QEMU 覆盖污染时也会造成「换个构建就卡住」的错觉。

`APP_SKIP_INPUT` / `APP_SKIP_EINK` + 独立 `-D SDKCONFIG=sdkconfig.qemu` 之后稳定跑通；
LittleFS 首次格式化在 QEMU 里约 1 秒，不是瓶颈。

---

## 九、实现结果（2026-09-18）

- 新增 `components/app/qemu_tour.c` + 两个 Kconfig（`APP_QEMU_TOUR` / `APP_QEMU_DUMP_B64`，默认 `n`）。
- 巡视实际产出 **17 帧**：表格的 11 行里，CONFIG 展开为 6 项，另补了「内容池为空」以对齐 `render-states.ps1`。
- 主机端 `tools/fb64-decode.ps1` 还原 PNG；一份样例已存到 `docs/desk-eink-info/qemu-tour/`。
- 验收：17 段 `FB64 BEGIN/END`，全部长度 15000；各帧黑像素分布符合预期
  （实心条约 12400 黑、反色/自适应不同、空池有占位、六项配置值块各异）。
