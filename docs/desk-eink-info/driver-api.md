# 驱动接口规格

> 更新：2026-09-16
> 状态：**草案**，供编码（opencode）参考
> 上游依据：`architecture.md`（模块划分）、`gpio-map.md`（引脚）、`protocol.md`（消息）

本文只定**函数签名与职责**，不写实现。命名与错误处理风格对齐 ESP-IDF 惯例。

---

## 通用约定

- 命名：`模块_动作()`，小写下划线
- 错误：返回 `esp_err_t`，成功 `ESP_OK`
- 初始化：每个模块提供 `xxx_init()`，在 `app_task` 启动时依次调用
- 头文件放 `components/<模块>/include/<模块>.h`
- 日志走 USB Serial/JTAG（UART0 被蓝牙占用）

---

## 一、eink — 墨水屏（SSD1619AZ）

```c
esp_err_t eink_init(void);

// framebuffer（400×300 单色，15000 字节）
void      fb_clear(bool white);
void      fb_set_pixel(int x, int y, bool black);
void      fb_draw_hline(int x0, int x1, int y, bool black);
void      fb_draw_vline(int x, int y0, int y1, bool black);
void      fb_draw_rect(int x, int y, int w, int h, bool black, bool fill);
void      fb_blit_1bit(const uint8_t *data, int x, int y, int w, int h, bool invert);

// 刷新
esp_err_t eink_flush_full(void);                      // 整屏刷新，含清残影
esp_err_t eink_flush_partial(int x, int y, int w, int h);  // 局部刷新（第二阶段）
```

**要点**

- `BUSY`（GPIO8）为高表示忙，每次刷新前要等它变低
- `RES#`（GPIO2）复位时序：拉低 ≥10ms → 拉高 → 等 ≥20ms
- 局部刷新依赖独立 LUT，**先不做**，接口先留着
- 反白：把目标区域像素取反即可，不需要额外字库

---

## 二、font — 点阵字库

```c
esp_err_t font_init(const uint8_t *cjk, size_t cjk_len,
                    const uint8_t *ascii, size_t ascii_len);

// 取字模：返回指向 32 字节（16×16）的指针；未收录返回 NULL
const uint8_t *font_get_glyph(uint32_t codepoint);

// 画一行 UTF-8 文本到 framebuffer；返回占用宽度（像素）
int font_draw_text(int x, int y, const char *utf8, bool invert);
int font_text_width(const char *utf8);
```

**要点**

- 汉字 16×16（32 字节/字），ASCII 8×16（16 字节/字）
- 字库格式见 `software/font/README.md`：magic + 码点升序表 + 点阵区
- 查找用二分，码点从 UTF-8 现解
- 缺字策略：画一个实心方块占位，并记日志

---

## 三、rtc — DS3231

```c
esp_err_t rtc_init(void);

esp_err_t rtc_get_time(struct tm *out);
esp_err_t rtc_set_time(const struct tm *t);

// 闹钟（对应 GPIO7 / EXT0）
esp_err_t rtc_set_alarm(const struct tm *t);
esp_err_t rtc_clear_alarm(void);
```

**要点**

- I2C 地址 `0x68`
- 唤醒后先读时间，再做别的
- 深睡前必须设好下一次闹钟

---

## 四、power — 电池电压（MCP3021）

```c
esp_err_t power_init(void);

esp_err_t power_read_mv(int *mv_out);      // 电池电压，毫伏
int       power_percent(int mv);           // 换算电量百分比（0~100）
```

**要点**

- I2C 地址 **`0x4B`**（A3T 版本，见 `software-notes.md`）
- 10-bit ADC：`v_ain = raw * 3300 / 1023`，电池电压 = `v_ain * 2`
- 低电量阈值 3.4V（见 `software-notes.md`）

---

## 五、input — 五向导航开关

```c
esp_err_t input_init(void);

typedef enum {
    INPUT_UP,            // 上
    INPUT_DOWN,          // 下
    INPUT_LEFT,          // 左
    INPUT_RIGHT,         // 右
    INPUT_CENTER_SHORT,  // 中央短按 <500ms
    INPUT_CENTER_LONG,   // 中央长按 >500ms
} input_event_t;

// 非阻塞取事件，无事件返回 false
bool input_poll(input_event_t *out);

// 深睡后被 GPIO0 EXT1 唤醒时返回 true
bool input_woke_from_switch(void);
```

**要点**

- **一个 GPIO 读出五个动作**：各方向串不同阻值到 COM，GPIO0（ADC1_CH0）读分压值查表判方向
- 深睡前 GPIO0 配成数字输入 + EXT1 唤醒（任意方向按下即拉低）；唤醒后切成 ADC 读方向，处理完切回
- 消抖在软件做（连续采样一致才确认）
- 中央短按 / 长按以 500ms 为界
- 状态映射见 `ui-states.md` 第二节、第五节；硬件见 `encoder-arch.md`

---

## 六、link — 蓝牙串口与协议

```c
esp_err_t link_init(void);

// 发送一条消息（自动序列化 + 追加换行）
esp_err_t link_send_schedule(const schedule_t *s);
esp_err_t link_send_todo(const todo_list_t *list);
esp_err_t link_send_req(const char *what);
esp_err_t link_send_hello(const char *fw_ver);
esp_err_t link_send_status(int bat_mv);

// 收到的消息通过回调交给 app 层
typedef void (*link_rx_cb_t)(const parsed_msg_t *msg);
void link_set_rx_callback(link_rx_cb_t cb);
```

**要点**

- 独占 UART0，运行在 `link_task`
- 逐行读取，行内解析 JSON，单行 ≤255 字节
- 解析失败只丢当前行，不中断
- 插画分片（`img_begin`/`img_chunk`/`img_end`）由本模块组包，完成后回调 app

---

## 七、store — 配置与文件

```c
esp_err_t store_init(void);

// NVS：小配置项
esp_err_t store_get_u32(const char *key, uint32_t *out);
esp_err_t store_set_u32(const char *key, uint32_t val);
esp_err_t store_get_str(const char *key, char *buf, size_t len);
esp_err_t store_set_str(const char *key, const char *val);

// LittleFS：大块数据（课表缓存 / 待办 / 插画 / 一言库）
esp_err_t store_write_file(const char *path, const void *data, size_t len);
esp_err_t store_read_file(const char *path, void *buf, size_t cap, size_t *out_len);
esp_err_t store_delete_file(const char *path);
```

**要点**

- NVS 放：**配置项**（刷新档位 / 番茄钟时长 / 小窗轮换，见 `ui-states.md` 附节）、番茄钟起始时刻、轮换计数、待办勾选状态
- LittleFS 放：课表 JSON 缓存、待办列表、插画文件、一言库
- 唤醒后先读缓存渲染，联网数据到了再覆盖

---

## 八、ui — 页面渲染（应用层，非驱动）

```c
void ui_render_idle(void);          // 课表条 + 待办 + 小窗
void ui_render_todo_nav(int cursor); // 待办区带光标
void ui_render_todo_edit(int cursor); // 待办区编辑态
void ui_render_pomo(int seconds_left); // 小窗番茄钟
void ui_render_menu(int cursor);              // 菜单叠加层：番茄钟 / 配置 / 取消
void ui_render_config(int item, int value);   // 配置页：当前项 + 当前值

// 局部重绘入口，避免整屏重画
void ui_redraw_schedule(void);
void ui_redraw_todo(int cursor);
void ui_redraw_panel(void);
```

**要点**

- 只负责往 framebuffer 画，不碰 SPI
- 画完由 `app_task` 决定整屏还是局部 `eink_flush_*`
- 布局坐标见 `ui-spec.md`；`ui_render_config` 的 item 取值见 `ui_config_item_t`，语义见 `ui-states.md` 附节

---

## 九、待定

1. **framebuffer 大小取舍**：整屏 15000 字节，C3 的 RAM 放得下，无需分块
2. **ADC 校准**：C3 的 ADC 有非线性，分压间隔要足够大；优先用校准接口，或软件标定阈值
3. **插画存储格式**：直接存 1-bit 原始数据，还是压缩（RLE）后存
4. **link 的 JSON 库**：cJSON 还是自写轻量解析（消息结构固定，后者更省）
