#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "eink.h"
#include "font.h"
#include "store.h"
#include "esp_log.h"

static const char *TAG = "ui";

/* ---------- 布局（ui-spec.md） ---------- */
#define SCHED_X   0
#define SCHED_Y   0
#define SCHED_W   400
#define SCHED_H   64

#define TODO_X    0
#define TODO_Y    64
#define TODO_W    266
#define TODO_H    236

#define PANEL_X   266
#define PANEL_Y   64
#define PANEL_W   134
#define PANEL_H   236

#define DATE_H    36
#define CONTENT_Y (PANEL_Y + DATE_H)   /* 100 */
#define CONTENT_H (PANEL_H - DATE_H)   /* 200 */

#define PAD       8
#define LINE_H    20

#define UI_BAR_H  32

/* 插画文件长度 → 尺寸（行按字节对齐，故有 padding） */
#define IMG_FULL_BYTES  (((400 + 7) / 8) * 300)   /* 15000 */
#define IMG_PANEL_BYTES (((134 + 7) / 8) * 200)   /* 3400 */

/* ---------- 内部状态 ---------- */
static ui_schedule_t  s_sched;
static ui_todo_list_t s_todo;
static int            s_year, s_month, s_day, s_weekday;
static int            s_clock_h = -1, s_clock_m = -1;
static int            s_art_mode;   /* 0 全屏 / 1 小窗 */
static int            s_bar_mode;   /* ui_bar_mode_t */

/* 内容池：文本槽固定，图片槽按 id 升序 */
static ui_item_t s_news;
static bool      s_has_news;
static ui_item_t s_quote;
static bool      s_has_quote;
static int       s_image_ids[UI_POOL_MAX];
static int       s_image_count;

static ui_item_t s_pool[UI_POOL_MAX];
static int       s_pool_count;
static int       s_pool_current;

static const char *const WEEKDAY_CN[7] = {
    "周日", "周一", "周二", "周三", "周四", "周五", "周六"
};

static const char *panel_title(ui_panel_kind_t kind)
{
    switch (kind) {
    case UI_PANEL_NEWS:  return "新闻";
    case UI_PANEL_IMAGE: return "插画";
    case UI_PANEL_QUOTE: return "每日一言";
    default:             return "";
    }
}

esp_err_t ui_init(void)
{
    memset(&s_sched, 0, sizeof(s_sched));
    memset(&s_todo, 0, sizeof(s_todo));
    s_year = 2026; s_month = 1; s_day = 1; s_weekday = 4;
    s_clock_h = -1; s_clock_m = -1;
    s_art_mode = 0;
    s_bar_mode = UI_BAR_SOLID;
    ui_pool_clear();
    ESP_LOGI(TAG, "ui ready");
    return ESP_OK;
}

void ui_set_schedule(const ui_schedule_t *s)
{
    if (s) {
        s_sched = *s;
    }
}

void ui_set_todo(const ui_todo_list_t *list)
{
    if (list) {
        s_todo = *list;
        if (s_todo.count > UI_TODO_MAX) {
            s_todo.count = UI_TODO_MAX;
        }
    }
}

void ui_set_date(int year, int month, int day, int weekday)
{
    s_year = year;
    s_month = month;
    s_day = day;
    s_weekday = ((weekday % 7) + 7) % 7;
}

void ui_set_clock(int hour, int minute)
{
    s_clock_h = hour;
    s_clock_m = minute;
}

void ui_set_art_mode(int mode)
{
    s_art_mode = mode;
}

void ui_set_bar_mode(int mode)
{
    s_bar_mode = mode;
}

/* ---------- 内容池 ---------- */

static void pool_rebuild(void)
{
    bool has_cur = (s_pool_count > 0 && s_pool_current >= 0 && s_pool_current < s_pool_count);
    ui_item_kind_t  ck  = has_cur ? s_pool[s_pool_current].kind : UI_ITEM_TEXT;
    ui_panel_kind_t ctk = has_cur ? s_pool[s_pool_current].text_kind : UI_PANEL_QUOTE;
    int cid = has_cur ? s_pool[s_pool_current].image_id : -1;

    int n = 0;
    if (s_has_news && n < UI_POOL_MAX) {
        s_pool[n++] = s_news;
    }
    if (s_has_quote && n < UI_POOL_MAX) {
        s_pool[n++] = s_quote;
    }
    for (int i = 0; i < s_image_count && n < UI_POOL_MAX; i++) {
        ui_item_t it = { 0 };
        it.kind = UI_ITEM_IMAGE;
        it.image_id = s_image_ids[i];
        s_pool[n++] = it;
    }
    s_pool_count = n;

    int cur = 0;
    if (has_cur) {
        for (int i = 0; i < n; i++) {
            bool match = (s_pool[i].kind == ck) &&
                         ((ck == UI_ITEM_TEXT) ? (s_pool[i].text_kind == ctk)
                                               : (s_pool[i].image_id == cid));
            if (match) {
                cur = i;
                break;
            }
        }
    }
    s_pool_current = (n > 0) ? cur : 0;
}

void ui_pool_clear(void)
{
    s_has_news = false;
    s_has_quote = false;
    s_image_count = 0;
    s_pool_count = 0;
    s_pool_current = 0;
}

void ui_pool_set_text(ui_panel_kind_t kind, const char *title, const char *body)
{
    ui_item_t it = { 0 };
    it.kind = UI_ITEM_TEXT;
    it.text_kind = kind;
    if (title) {
        strlcpy(it.title, title, sizeof(it.title));
    }
    if (body) {
        strlcpy(it.body, body, sizeof(it.body));
    }
    if (kind == UI_PANEL_QUOTE) {
        s_quote = it;
        s_has_quote = true;
    } else {
        s_news = it;
        s_has_news = true;
    }
    pool_rebuild();
}

void ui_pool_set_images(const int *ids, int count)
{
    if (ids == NULL || count <= 0) {
        s_image_count = 0;
        pool_rebuild();
        return;
    }
    if (count > UI_POOL_MAX) {
        count = UI_POOL_MAX;
    }
    for (int i = 0; i < count; i++) {
        s_image_ids[i] = ids[i];
    }
    s_image_count = count;
    pool_rebuild();
}

int ui_pool_count(void)
{
    return s_pool_count;
}

int ui_pool_current(void)
{
    return s_pool_current;
}

void ui_pool_set_current(int index)
{
    if (s_pool_count <= 0) {
        s_pool_current = 0;
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= s_pool_count) {
        index = s_pool_count - 1;
    }
    s_pool_current = index;
}

void ui_pool_move(int delta)
{
    if (s_pool_count <= 0) {
        return;
    }
    s_pool_current = ((s_pool_current + delta) % s_pool_count + s_pool_count) % s_pool_count;
}

/* ---------- 绘制辅助 ---------- */

static void clear_region(int x, int y, int w, int h)
{
    fb_draw_rect(x, y, w, h, false, true);
}

/* 小窗左侧竖线在 x=266，清理区域会盖掉它，重画 */
static void draw_panel_border(int y, int h)
{
    fb_draw_vline(PANEL_X, y, y + h - 1, true);
}

static uint32_t utf8_decode(const char *s, int *clen)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t c = p[0];
    if (c < 0x80) {
        *clen = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        *clen = 2;
        return ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        *clen = 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        *clen = 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
               ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
    *clen = 1;
    return c;
}

/* 禁则：行首不能是收尾标点 */
static bool is_no_start(uint32_t cp)
{
    switch (cp) {
    case 0x3002: /* 。 */ case 0xFF0C: /* ， */ case 0x3001: /* 、 */
    case 0xFF01: /* ！ */ case 0xFF1F: /* ？ */ case 0xFF09: /* ） */
    case 0x300B: /* 》 */ case 0x300D: /* 」 */ case 0x300F: /* 』 */
    case 0x3011: /* 】 */ case 0x201D: /* ” */ case 0x2019: /* ’ */
    case 0xFF1A: /* ： */ case 0xFF1B: /* ； */ case 0x3015: /* 〕 */
        return true;
    default:
        return false;
    }
}

/* 禁则：行尾不能是开引号 / 开括号 */
static bool is_no_end(uint32_t cp)
{
    switch (cp) {
    case 0x201C: /* “ */ case 0x2018: /* ‘ */ case 0xFF08: /* （ */
    case 0x300A: /* 《 */ case 0x300C: /* 「 */ case 0x300E: /* 『 */
    case 0x3010: /* 【 */ case 0x3014: /* 〔 */
        return true;
    default:
        return false;
    }
}

/* 按像素宽度自动换行，含禁则处理；返回实际绘制行数 */
static int draw_wrapped(int x, int y, int max_w, const char *text, int line_h, int max_lines)
{
    if (text == NULL) {
        return 0;
    }
    char line[128];
    int li = 0, line_w = 0, lines = 0;
    int last_len = 0, last_w = 0;   /* 行尾字符 */
    uint32_t last_cp = 0;
    const char *p = text;

    while (*p && lines < max_lines) {
        int clen = 0;
        uint32_t cp = utf8_decode(p, &clen);
        int cw = (cp < 0x80) ? 8 : 16;

        /* 收尾标点不另起一行，允许本行轻微溢出 */
        if (line_w + cw > max_w && li > 0 && !is_no_start(cp)) {
            if (last_cp && is_no_end(last_cp) && li - last_len > 0) {
                /* 行尾是开引号 / 开括号：挪到下一行 */
                int mv_len = last_len, mv_w = last_w;
                li -= mv_len;
                line_w -= mv_w;
                line[li] = '\0';
                font_draw_text(x, y + lines * line_h, line, false);
                lines++;
                if (lines >= max_lines) {
                    break;
                }
                memcpy(line, p - mv_len, mv_len);
                li = mv_len;
                line_w = mv_w;
                last_cp = 0;
                last_len = 0;
                last_w = 0;
            } else {
                line[li] = '\0';
                font_draw_text(x, y + lines * line_h, line, false);
                lines++;
                li = 0;
                line_w = 0;
                last_cp = 0;
                last_len = 0;
                last_w = 0;
                if (lines >= max_lines) {
                    break;
                }
            }
        }

        if (li + clen < (int)sizeof(line)) {
            memcpy(&line[li], p, clen);
            li += clen;
            line_w += cw;
            last_cp = cp;
            last_len = clen;
            last_w = cw;
        }
        p += clen;
    }

    if (li > 0 && lines < max_lines) {
        line[li] = '\0';
        font_draw_text(x, y + lines * line_h, line, false);
        lines++;
    }
    return lines;
}

static int hm_to_min(const char *s)
{
    if (s == NULL || strlen(s) < 5) {
        return -1;
    }
    return (s[0] - '0') * 600 + (s[1] - '0') * 60 + (s[3] - '0') * 10 + (s[4] - '0');
}

/* 直接读写 framebuffer，供反色/统计用 */
static bool fb_get(int x, int y)
{
    if (x < 0 || x >= EINK_WIDTH || y < 0 || y >= EINK_HEIGHT) {
        return false;
    }
    const uint8_t *fb = eink_framebuffer();
    return ((fb[y * EINK_FB_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1) != 0;
}

static int count_dark(int x, int y, int w, int h)
{
    int c = 0;
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (fb_get(xx, yy)) {
                c++;
            }
        }
    }
    return c;
}

static void invert_rect(int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            fb_set_pixel(xx, yy, !fb_get(xx, yy));
        }
    }
}

/* ---------- 课表长条 ---------- */
static void draw_schedule(void)
{
    clear_region(SCHED_X, SCHED_Y, SCHED_W, SCHED_H);
    fb_draw_hline(SCHED_X, SCHED_X + SCHED_W - 1, SCHED_Y + SCHED_H - 1, true);

    char buf[96];

    if (s_sched.cur_name[0] == '\0') {
        font_draw_text(PAD, 4, "暂无课程", false);
        return;
    }

    if (s_sched.cur_period > 0) {
        snprintf(buf, sizeof(buf), "第%d节 %s", s_sched.cur_period, s_sched.cur_name);
    } else {
        snprintf(buf, sizeof(buf), "%s", s_sched.cur_name);
    }
    font_draw_text(PAD, 4, buf, false);

    int total = -1;
    int start = hm_to_min(s_sched.cur_start);
    int end = hm_to_min(s_sched.cur_end);
    if (start >= 0 && end >= 0 && end >= start) {
        total = end - start;
    }
    if (total >= 0) {
        snprintf(buf, sizeof(buf), "%s-%s · 共%d分 · 剩余约%d分",
                 s_sched.cur_start, s_sched.cur_end, total, s_sched.cur_left);
    } else {
        snprintf(buf, sizeof(buf), "%s-%s · 剩余约%d分",
                 s_sched.cur_start, s_sched.cur_end, s_sched.cur_left);
    }
    font_draw_text(PAD, 24, buf, false);

    if (s_sched.next_name[0]) {
        snprintf(buf, sizeof(buf), "下一节 %s %s-%s",
                 s_sched.next_name, s_sched.next_start, s_sched.next_end);
    } else {
        snprintf(buf, sizeof(buf), "下一节 —");
    }
    font_draw_text(PAD, 44, buf, false);
}

/* ---------- 待办 ---------- */
static void draw_checkbox(int x, int y, bool done)
{
    fb_draw_rect(x, y, 13, 13, true, false);
    if (done) {
        fb_set_pixel(x + 3, y + 6, true);
        fb_set_pixel(x + 4, y + 7, true);
        fb_set_pixel(x + 5, y + 8, true);
        fb_set_pixel(x + 6, y + 7, true);
        fb_set_pixel(x + 7, y + 6, true);
        fb_set_pixel(x + 8, y + 5, true);
        fb_set_pixel(x + 9, y + 4, true);
    }
}

static void draw_todo(int cursor, bool editing)
{
    clear_region(TODO_X, TODO_Y, TODO_W, TODO_H);
    fb_draw_vline(PANEL_X, TODO_Y, TODO_Y + TODO_H - 1, true);

    font_draw_text(PAD, TODO_Y + 8, "待办", false);

    if (s_todo.count == 0) {
        font_draw_text(PAD, TODO_Y + 35, "（暂无待办）", false);
        return;
    }

    int iy = TODO_Y + 35;
    for (int i = 0; i < s_todo.count; i++) {
        int y = iy + i * 30;
        if (i == cursor) {
            fb_draw_rect(2, y, 3, 13, true, true);
        }
        draw_checkbox(PAD, y, s_todo.items[i].done);
        font_draw_text(30, y - 3, s_todo.items[i].text, false);
        if (editing && i == cursor) {
            int w = font_text_width(s_todo.items[i].text);
            fb_draw_rect(30 + w, y - 3, 8, 16, true, true);
        }
    }
}

/* ---------- 小窗 ---------- */
static void draw_date(void)
{
    clear_region(PANEL_X, PANEL_Y, PANEL_W, DATE_H);
    draw_panel_border(PANEL_Y, DATE_H);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %s",
             s_year, s_month, s_day, WEEKDAY_CN[s_weekday]);
    font_draw_text(PANEL_X + PAD, PANEL_Y + 8, buf, false);
    fb_draw_hline(PANEL_X, PANEL_X + PANEL_W - 1, CONTENT_Y - 1, true);
}

/* 插画：从 LittleFS 读裸位图，按长度判定尺寸 */
static const uint8_t *load_image(int image_id, size_t *len)
{
    static uint8_t buf[IMG_FULL_BYTES];
    char path[32];
    snprintf(path, sizeof(path), "img_%d.bin", image_id);
    size_t n = 0;
    if (store_read_file(path, buf, sizeof(buf), &n) != ESP_OK) {
        ESP_LOGW(TAG, "image %d read failed", image_id);
        return NULL;
    }
    *len = n;
    return buf;
}

void ui_draw_image_full(int image_id)
{
    size_t len = 0;
    const uint8_t *data = load_image(image_id, &len);
    if (data == NULL) {
        return;
    }
    if (len != IMG_FULL_BYTES) {
        ESP_LOGW(TAG, "img %d size %u != %d, skip", image_id, (unsigned)len, IMG_FULL_BYTES);
        return;
    }
    fb_blit_1bit(data, 0, 0, 400, 300, false);
}

void ui_draw_image_panel(int image_id)
{
    size_t len = 0;
    const uint8_t *data = load_image(image_id, &len);
    if (data == NULL) {
        return;
    }
    if (len != IMG_PANEL_BYTES) {
        ESP_LOGW(TAG, "img %d size %u != %d, skip", image_id, (unsigned)len, IMG_PANEL_BYTES);
        return;
    }
    fb_blit_1bit(data, PANEL_X, CONTENT_Y, 134, 200, false);
}

/* 当前池项 → 小窗内容区 */
static void draw_panel(void)
{
    clear_region(PANEL_X, CONTENT_Y, PANEL_W, CONTENT_H);
    draw_panel_border(CONTENT_Y, CONTENT_H);

    if (s_pool_count == 0) {
        font_draw_text(PANEL_X + PAD, CONTENT_Y + 8, "暂无内容", false);
        return;
    }

    const ui_item_t *it = &s_pool[s_pool_current];
    if (it->kind == UI_ITEM_IMAGE) {
        ui_draw_image_panel(it->image_id);
        return;
    }

    const char *title = it->title[0] ? it->title : panel_title(it->text_kind);
    font_draw_text(PANEL_X + PAD, CONTENT_Y + 8, title, false);
    fb_draw_hline(PANEL_X + PAD, PANEL_X + PANEL_W - PAD, CONTENT_Y + 26, true);
    draw_wrapped(PANEL_X + PAD, CONTENT_Y + 36, PANEL_W - 2 * PAD,
                 it->body, LINE_H, (CONTENT_H - 46) / LINE_H);
}

static void draw_pomo(int seconds_left)
{
    clear_region(PANEL_X, CONTENT_Y, PANEL_W, CONTENT_H);
    draw_panel_border(CONTENT_Y, CONTENT_H);
    if (seconds_left < 0) {
        seconds_left = 0;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", seconds_left / 60, seconds_left % 60);

    font_draw_text(PANEL_X + PAD, CONTENT_Y + 8, "番茄钟", false);
    fb_draw_hline(PANEL_X + PAD, PANEL_X + PANEL_W - PAD, CONTENT_Y + 26, true);
    font_draw_text(PANEL_X + PAD + 24, CONTENT_Y + 70, buf, false);
    font_draw_text(PANEL_X + PAD + 12, CONTENT_Y + 110, "中央键暂停", false);
}

/* ---------- 信息条（全屏插画时） ---------- */

static void draw_segment_auto(int x, int y, const char *text)
{
    if (text[0] == '\0') {
        return;
    }
    int w = font_text_width(text);
    int bx = x - 2, by = y, bw = w + 4, bh = 16;
    if (bx < 0) { bw += bx; bx = 0; }
    if (by < 0) { bh += by; by = 0; }
    if (bx + bw > EINK_WIDTH)  bw = EINK_WIDTH - bx;
    if (by + bh > EINK_HEIGHT) bh = EINK_HEIGHT - by;
    if (bw <= 0 || bh <= 0) {
        return;
    }

    int d0 = count_dark(bx, by, bw, bh);
    invert_rect(bx, by, bw, bh);
    int dark_after = bw * bh - d0;
    bool white = (dark_after * 2 > bw * bh);
    font_draw_text(x, y, text, white);
}

static void draw_info_bar(void)
{
    char left[16] = { 0 };
    char right[64] = { 0 };

    if (s_clock_h >= 0) {
        snprintf(left, sizeof(left), "%02d:%02d", s_clock_h, s_clock_m);
    }
    if (s_sched.cur_name[0]) {
        if (s_sched.cur_period > 0) {
            snprintf(right, sizeof(right), "第%d节 %s", s_sched.cur_period, s_sched.cur_name);
        } else {
            snprintf(right, sizeof(right), "%s", s_sched.cur_name);
        }
    }

    const int lx = 8, ly = 8;
    const int rx = EINK_WIDTH - 8 - font_text_width(right), ry = 8;

    switch (s_bar_mode) {
    case UI_BAR_SOLID:
        fb_draw_rect(0, 0, EINK_WIDTH, UI_BAR_H, true, true);
        if (left[0])  font_draw_text(lx, ly, left, true);
        if (right[0]) font_draw_text(rx, ry, right, true);
        break;
    case UI_BAR_INVERT:
        invert_rect(0, 0, EINK_WIDTH, UI_BAR_H);
        if (left[0])  font_draw_text(lx, ly, left, true);
        if (right[0]) font_draw_text(rx, ry, right, true);
        break;
    case UI_BAR_INVERT_AUTO:
    default:
        draw_segment_auto(lx, ly, left);
        draw_segment_auto(rx, ry, right);
        break;
    }
}

/* ---------- 菜单 / 配置页 ---------- */

#define MENU_ITEM_COUNT 3

static const char *const MENU_LABELS[MENU_ITEM_COUNT] = { "番茄钟", "配置", "取消" };

static const char *config_item_name(int item)
{
    switch (item) {
    case UI_CFG_REFRESH:    return "刷新档位";
    case UI_CFG_POMO_FOCUS: return "番茄钟专注";
    case UI_CFG_POMO_BREAK: return "番茄钟休息";
    case UI_CFG_ROTATE:     return "小窗轮换";
    case UI_CFG_ART_MODE:   return "插画显示";
    case UI_CFG_BAR_MODE:   return "信息条";
    default:                return "?";
    }
}

static void config_value_label(int item, int value, char *out, size_t n)
{
    switch (item) {
    case UI_CFG_REFRESH:
        snprintf(out, n, "%s", value == 0 ? "自动" : (value == 1 ? "常规" : "静默"));
        break;
    case UI_CFG_POMO_FOCUS:
    case UI_CFG_POMO_BREAK:
        snprintf(out, n, "%d 分", value);
        break;
    case UI_CFG_ROTATE:
        snprintf(out, n, "%s", value ? "开" : "关");
        break;
    case UI_CFG_ART_MODE:
        snprintf(out, n, "%s", value == 0 ? "全屏" : "小窗");
        break;
    case UI_CFG_BAR_MODE:
        snprintf(out, n, "%s", value == 0 ? "实心" : (value == 1 ? "反色" : "反色自适应"));
        break;
    default:
        snprintf(out, n, "%d", value);
        break;
    }
}

void ui_render_menu(int cursor)
{
    ui_render_idle();

    const int bx = 130, by = 80, bw = 140, bh = 130;
    fb_draw_rect(bx + 1, by + 1, bw - 2, bh - 2, false, true);   /* 清底 */
    fb_draw_rect(bx, by, bw, bh, true, false);                   /* 边框 */

    font_draw_text(bx + 10, by + 8, "菜单", false);
    fb_draw_hline(bx + 8, bx + bw - 8, by + 28, true);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int y = by + 42 + i * 26;
        bool sel = (i == cursor);
        if (sel) {
            fb_draw_rect(bx + 4, y - 4, bw - 8, 22, true, true);
        }
        font_draw_text(bx + 14, y, MENU_LABELS[i], sel);
    }
}

void ui_render_config(int item, int value)
{
    clear_region(0, 0, EINK_WIDTH, EINK_HEIGHT);

    char idx[16];
    snprintf(idx, sizeof(idx), "%d/%d", item + 1, UI_CFG_COUNT);
    font_draw_text(PAD, 10, "配置", false);
    font_draw_text(EINK_WIDTH - PAD - font_text_width(idx), 10, idx, false);
    fb_draw_hline(PAD, EINK_WIDTH - PAD, 36, true);

    const char *name = config_item_name(item);
    font_draw_text((EINK_WIDTH - font_text_width(name)) / 2, 84, name, false);

    char val[24];
    config_value_label(item, value, val, sizeof(val));
    int vw = font_text_width(val);
    int vx = (EINK_WIDTH - vw) / 2;
    fb_draw_rect(vx - 14, 124, vw + 28, 30, true, true);   /* 反白块 */
    font_draw_text(vx, 131, val, true);

    fb_draw_hline(PAD, EINK_WIDTH - PAD, 222, true);
    font_draw_text(PAD, 240, "上/下 换项", false);
    font_draw_text(PAD, 262, "左/右 调值", false);
    const char *r1 = "中央短按保存";
    const char *r2 = "长按返回";
    font_draw_text(EINK_WIDTH - PAD - font_text_width(r1), 240, r1, false);
    font_draw_text(EINK_WIDTH - PAD - font_text_width(r2), 262, r2, false);
}

/* ---------- 对外渲染入口 ---------- */

void ui_render_idle(void)
{
    bool full_image = false;
    if (s_pool_count > 0 && s_pool[s_pool_current].kind == UI_ITEM_IMAGE && s_art_mode == 0) {
        full_image = true;
    }

    if (full_image) {
        fb_clear(true);
        ui_draw_image_full(s_pool[s_pool_current].image_id);
        draw_info_bar();
        return;
    }

    draw_schedule();
    draw_todo(-1, false);
    draw_date();
    draw_panel();
}

void ui_render_todo_nav(int cursor)
{
    draw_todo(cursor, false);
}

void ui_render_todo_edit(int cursor)
{
    draw_todo(cursor, true);
}

void ui_render_pomo(int seconds_left)
{
    draw_pomo(seconds_left);
}

void ui_redraw_schedule(void)
{
    draw_schedule();
}

void ui_redraw_todo(int cursor)
{
    draw_todo(cursor, false);
}

void ui_redraw_panel(void)
{
    draw_date();
    draw_panel();
}
