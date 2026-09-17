#include "ui.h"

#include <stdio.h>
#include <string.h>
#include "eink.h"
#include "font.h"
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

/* ---------- 内部状态 ---------- */
static ui_schedule_t  s_sched;
static ui_todo_list_t s_todo;
static ui_panel_t     s_panel;
static int            s_year, s_month, s_day, s_weekday;

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
    memset(&s_panel, 0, sizeof(s_panel));
    s_panel.kind = UI_PANEL_QUOTE;
    s_year = 2026; s_month = 1; s_day = 1; s_weekday = 4;
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

void ui_set_panel(const ui_panel_t *p)
{
    if (p) {
        s_panel = *p;
    }
}

void ui_set_date(int year, int month, int day, int weekday)
{
    s_year = year;
    s_month = month;
    s_day = day;
    s_weekday = ((weekday % 7) + 7) % 7;
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

static int utf8_char_len(uint8_t c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* 按像素宽度自动换行；返回实际绘制行数 */
static int draw_wrapped(int x, int y, int max_w, const char *text, int line_h, int max_lines)
{
    if (text == NULL) {
        return 0;
    }
    char line[128];
    int li = 0;
    int line_w = 0;
    int lines = 0;
    const char *p = text;

    while (*p && lines < max_lines) {
        uint8_t c = (uint8_t)*p;
        int clen = utf8_char_len(c);
        int cw = (c < 0x80) ? 8 : 16;

        if (line_w + cw > max_w && li > 0) {
            line[li] = '\0';
            font_draw_text(x, y + lines * line_h, line, false);
            lines++;
            li = 0;
            line_w = 0;
            if (lines >= max_lines) {
                break;
            }
        }
        if (li + clen < (int)sizeof(line)) {
            memcpy(&line[li], p, clen);
            li += clen;
            line_w += cw;
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
        /* 对勾 */
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
            /* 光标指示条 */
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

static void draw_panel_content(void)
{
    clear_region(PANEL_X, CONTENT_Y, PANEL_W, CONTENT_H);
    draw_panel_border(CONTENT_Y, CONTENT_H);

    const char *title = s_panel.title[0] ? s_panel.title : panel_title(s_panel.kind);
    font_draw_text(PANEL_X + PAD, CONTENT_Y + 8, title, false);
    fb_draw_hline(PANEL_X + PAD, PANEL_X + PANEL_W - PAD, CONTENT_Y + 26, true);

    draw_wrapped(PANEL_X + PAD, CONTENT_Y + 36, PANEL_W - 2 * PAD,
                 s_panel.body, LINE_H, (CONTENT_H - 46) / LINE_H);
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
    draw_schedule();
    draw_todo(-1, false);
    draw_date();
    draw_panel_content();
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
    draw_panel_content();
}
