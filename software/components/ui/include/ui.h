#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * ui — 页面布局与绘制（只往 framebuffer 画，不碰 SPI）
 *
 * 布局见 ui-spec.md：
 *   课表长条   (0,0,400,64)      横跨整屏
 *   待办       (0,64,266,236)
 *   小窗日期栏 (266,64,134,36)
 *   小窗内容区 (266,100,134,200)
 */

typedef struct {
    int  state;              /* 0=无 1=上课 2=准备上课 3=课间 4=放学 */
    int  cur_period;         /* 节次，0 表示未知 */
    char cur_name[32];
    char cur_initial[8];
    char cur_start[6];       /* "HH:MM" */
    char cur_end[6];
    int  cur_left;           /* 剩余分钟 */
    char next_name[32];
    char next_start[6];
    char next_end[6];
} ui_schedule_t;

typedef struct {
    int  id;
    bool done;
    char text[48];
} ui_todo_item_t;

#define UI_TODO_MAX 8

typedef struct {
    int            count;
    ui_todo_item_t items[UI_TODO_MAX];
} ui_todo_list_t;

typedef enum {
    UI_PANEL_NEWS,
    UI_PANEL_IMAGE,
    UI_PANEL_QUOTE,
} ui_panel_kind_t;

/* 配置页项目（顺序即上/下切换顺序，见 ui-states.md 附节） */
typedef enum {
    UI_CFG_REFRESH = 0,     /* 刷新档位：0 自动 / 1 常规 / 2 静默 */
    UI_CFG_POMO_FOCUS,      /* 番茄钟专注：15 / 25 / 45 分 */
    UI_CFG_POMO_BREAK,      /* 番茄钟休息：5 / 10 分 */
    UI_CFG_ROTATE,          /* 小窗轮换：0 关 / 1 开 */
    UI_CFG_ART_MODE,        /* 插画显示：0 全屏 / 1 小窗 */
    UI_CFG_BAR_MODE,        /* 信息条：0 实心 / 1 反色 / 2 反色自适应 */
    UI_CFG_COUNT,
} ui_config_item_t;

/* ---------- 小窗内容池 ---------- */
#define UI_POOL_MAX 8

typedef enum {
    UI_ITEM_TEXT,           /* 文本：新闻 / 一言 */
    UI_ITEM_IMAGE,          /* 位图：插画 */
} ui_item_kind_t;

typedef struct {
    ui_item_kind_t  kind;
    ui_panel_kind_t text_kind;    /* 文本项有效 */
    char            title[24];
    char            body[256];
    int             image_id;     /* 位图项有效 */
} ui_item_t;

/* 信息条模式（全屏插画时） */
typedef enum {
    UI_BAR_SOLID = 0,       /* 实心黑条 */
    UI_BAR_INVERT = 1,      /* 反色整条 */
    UI_BAR_INVERT_AUTO = 2, /* 反色文字块 + 自适应字色 */
} ui_bar_mode_t;

esp_err_t ui_init(void);

/* 数据注入（由 app / link 层调用） */
void ui_set_schedule(const ui_schedule_t *s);
void ui_set_todo(const ui_todo_list_t *list);
void ui_set_date(int year, int month, int day, int weekday);  /* weekday: 0=周日 */
void ui_set_clock(int hour, int minute);                      /* 信息条用；未知传 -1 */
void ui_set_art_mode(int mode);                               /* 0 全屏 / 1 小窗 */
void ui_set_bar_mode(int mode);                               /* ui_bar_mode_t */

/* 内容池：顺序固定为 新闻 → 一言 → 插画（按 id 升序） */
void ui_pool_clear(void);
void ui_pool_set_text(ui_panel_kind_t kind, const char *title, const char *body);
void ui_pool_set_images(const int *ids, int count);
int  ui_pool_count(void);
int  ui_pool_current(void);
void ui_pool_set_current(int index);
void ui_pool_move(int delta);                                 /* ±1，环形 */

/* 插画绘制 */
void ui_draw_image_full(int image_id);   /* 铺 (0,0)，400×300 */
void ui_draw_image_panel(int image_id);  /* 铺小窗内容区 (266,100,134,200) */

/* 整屏渲染 */
void ui_render_idle(void);
void ui_render_todo_nav(int cursor);
void ui_render_todo_edit(int cursor);
void ui_render_pomo(int seconds_left);
void ui_render_menu(int cursor);              /* 菜单叠加层：番茄钟 / 配置 / 取消 */
void ui_render_config(int item, int value);   /* 配置页：当前项 + 当前值 */

/* 局部重绘入口（只重画 framebuffer 对应区域） */
void ui_redraw_schedule(void);
void ui_redraw_todo(int cursor);
void ui_redraw_panel(void);
