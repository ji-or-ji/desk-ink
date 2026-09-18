#include "app.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "eink.h"
#include "ui.h"
#include "input.h"
#include "link.h"
#include "store.h"
#include "rtc.h"
#include "power.h"

static const char *TAG = "app";

#define APP_FW_VER "0.1.0"

/* ---------- 配置项（NVS namespace "deskink"，见 变更-配置页.md / 变更-显示侧.md） ---------- */
#define CFG_KEY_REFRESH    "cfg_refresh"
#define CFG_KEY_POMO_FOCUS "cfg_pomo_focus"
#define CFG_KEY_POMO_BREAK "cfg_pomo_break"
#define CFG_KEY_ROTATE     "cfg_rotate"
#define CFG_KEY_ART_MODE   "cfg_art_mode"
#define CFG_KEY_BAR_MODE   "cfg_bar_mode"

#define CFG_REFRESH_DEFAULT   1     /* 常规 */
#define CFG_FOCUS_DEFAULT     25
#define CFG_BREAK_DEFAULT     5
#define CFG_ROTATE_DEFAULT    1     /* 开 */
#define CFG_ART_MODE_DEFAULT  0     /* 全屏 */
#define CFG_BAR_MODE_DEFAULT  0     /* 实心 */

static const int REFRESH_VALS[] = { 0, 1, 2 };        /* 自动 / 常规 / 静默 */
static const int FOCUS_VALS[]   = { 15, 25, 45 };
static const int BREAK_VALS[]   = { 5, 10 };
static const int ROTATE_VALS[]  = { 0, 1 };
static const int ART_VALS[]     = { 0, 1 };           /* 全屏 / 小窗 */
static const int BAR_VALS[]     = { 0, 1, 2 };        /* 实心 / 反色 / 反色自适应 */

/* 内容池持久化 key */
#define POOL_KEY_NEWS_TITLE  "pool_news_title"
#define POOL_KEY_NEWS_BODY   "pool_news_body"
#define POOL_KEY_QUOTE_TITLE "pool_quote_title"
#define POOL_KEY_QUOTE_BODY  "pool_quote_body"
#define POOL_KEY_CUR         "pool_cur"
#define TODO_KEY             "todo_list"

typedef struct {
    int refresh;
    int pomo_focus;
    int pomo_break;
    int rotate;
    int art_mode;
    int bar_mode;
} app_config_t;

/* ---------- 状态机 ---------- */
typedef enum {
    ST_IDLE,
    ST_MENU,
    ST_CONFIG,
    ST_TODO_NAV,
    ST_TODO_EDIT,
    ST_POMO,
} app_state_t;

#define MENU_POMO   0
#define MENU_CONFIG 1
#define MENU_CANCEL 2
#define MENU_COUNT  3

#define TICK_MS        100
#define TIMEOUT_NORMAL (300 / TICK_MS)   /* 30s */
#define TIMEOUT_EDIT   (600 / TICK_MS)   /* 60s */
#define POWER_READ_TICKS (30 * 60 * 1000 / TICK_MS)   /* 每 30 分钟读一次电量 */

static app_config_t   s_cfg;
static ui_todo_list_t s_todos;
static app_state_t    s_state;
static int            s_menu_cursor;
static int            s_cfg_item;
static int            s_cfg_value;
static int            s_todo_cursor;
static int            s_pomo_left;
static bool           s_pomo_paused;
static int            s_idle_ticks;
static int            s_power_ticks;
static QueueHandle_t  s_msgq;

/* ---------- 配置读写 ---------- */

static uint32_t cfg_load_u32(const char *key, uint32_t def)
{
    uint32_t v = def;
    if (store_get_u32(key, &v) != ESP_OK) {
        v = def;
    }
    return v;
}

static void config_load(void)
{
    s_cfg.refresh    = (int)cfg_load_u32(CFG_KEY_REFRESH, CFG_REFRESH_DEFAULT);
    s_cfg.pomo_focus = (int)cfg_load_u32(CFG_KEY_POMO_FOCUS, CFG_FOCUS_DEFAULT);
    s_cfg.pomo_break = (int)cfg_load_u32(CFG_KEY_POMO_BREAK, CFG_BREAK_DEFAULT);
    s_cfg.rotate     = (int)cfg_load_u32(CFG_KEY_ROTATE, CFG_ROTATE_DEFAULT);
    s_cfg.art_mode   = (int)cfg_load_u32(CFG_KEY_ART_MODE, CFG_ART_MODE_DEFAULT);
    s_cfg.bar_mode   = (int)cfg_load_u32(CFG_KEY_BAR_MODE, CFG_BAR_MODE_DEFAULT);
}

static const char *config_key(int item)
{
    switch (item) {
    case UI_CFG_REFRESH:    return CFG_KEY_REFRESH;
    case UI_CFG_POMO_FOCUS: return CFG_KEY_POMO_FOCUS;
    case UI_CFG_POMO_BREAK: return CFG_KEY_POMO_BREAK;
    case UI_CFG_ROTATE:     return CFG_KEY_ROTATE;
    case UI_CFG_ART_MODE:   return CFG_KEY_ART_MODE;
    case UI_CFG_BAR_MODE:   return CFG_KEY_BAR_MODE;
    default:                return NULL;
    }
}

static int config_get_value(int item)
{
    switch (item) {
    case UI_CFG_REFRESH:    return s_cfg.refresh;
    case UI_CFG_POMO_FOCUS: return s_cfg.pomo_focus;
    case UI_CFG_POMO_BREAK: return s_cfg.pomo_break;
    case UI_CFG_ROTATE:     return s_cfg.rotate;
    case UI_CFG_ART_MODE:   return s_cfg.art_mode;
    case UI_CFG_BAR_MODE:   return s_cfg.bar_mode;
    default:                return 0;
    }
}

static void config_set_value(int item, int val)
{
    switch (item) {
    case UI_CFG_REFRESH:    s_cfg.refresh = val;    break;
    case UI_CFG_POMO_FOCUS: s_cfg.pomo_focus = val; break;
    case UI_CFG_POMO_BREAK: s_cfg.pomo_break = val; break;
    case UI_CFG_ROTATE:     s_cfg.rotate = val;     break;
    case UI_CFG_ART_MODE:   s_cfg.art_mode = val;   break;
    case UI_CFG_BAR_MODE:   s_cfg.bar_mode = val;   break;
    default: break;
    }
}

static const int *config_values(int item, int *n)
{
    switch (item) {
    case UI_CFG_REFRESH:    *n = (int)(sizeof(REFRESH_VALS) / sizeof(int)); return REFRESH_VALS;
    case UI_CFG_POMO_FOCUS: *n = (int)(sizeof(FOCUS_VALS) / sizeof(int));   return FOCUS_VALS;
    case UI_CFG_POMO_BREAK: *n = (int)(sizeof(BREAK_VALS) / sizeof(int));   return BREAK_VALS;
    case UI_CFG_ROTATE:     *n = (int)(sizeof(ROTATE_VALS) / sizeof(int));  return ROTATE_VALS;
    case UI_CFG_ART_MODE:   *n = (int)(sizeof(ART_VALS) / sizeof(int));     return ART_VALS;
    case UI_CFG_BAR_MODE:   *n = (int)(sizeof(BAR_VALS) / sizeof(int));     return BAR_VALS;
    default:                *n = 0; return NULL;
    }
}

static int config_cycle(int item, int cur, int dir)
{
    int n = 0;
    const int *vals = config_values(item, &n);
    if (vals == NULL || n <= 0) {
        return cur;
    }
    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (vals[i] == cur) {
            idx = i;
            break;
        }
    }
    idx = (idx + (dir > 0 ? 1 : -1) + n) % n;
    return vals[idx];
}

static void config_save_item(int item)
{
    const char *key = config_key(item);
    if (key) {
        store_set_u32(key, (uint32_t)config_get_value(item));
    }
}

static void config_save_all(void)
{
    store_set_u32(CFG_KEY_REFRESH, (uint32_t)s_cfg.refresh);
    store_set_u32(CFG_KEY_POMO_FOCUS, (uint32_t)s_cfg.pomo_focus);
    store_set_u32(CFG_KEY_POMO_BREAK, (uint32_t)s_cfg.pomo_break);
    store_set_u32(CFG_KEY_ROTATE, (uint32_t)s_cfg.rotate);
    store_set_u32(CFG_KEY_ART_MODE, (uint32_t)s_cfg.art_mode);
    store_set_u32(CFG_KEY_BAR_MODE, (uint32_t)s_cfg.bar_mode);
}

/* 把配置推给 ui（信息条模式 / 插画模式） */
static void apply_display_modes(void)
{
    ui_set_art_mode(s_cfg.art_mode);
    ui_set_bar_mode(s_cfg.bar_mode);
}

/* 把当前配置全量报给 PC（hello 之后 / 配置页保存之后） */
static void push_cfg(void)
{
    link_cfg_t c = {
        .refresh    = s_cfg.refresh,
        .pomo_focus = s_cfg.pomo_focus,
        .pomo_break = s_cfg.pomo_break,
        .rotate     = s_cfg.rotate,
        .art_mode   = s_cfg.art_mode,
        .bar_mode   = s_cfg.bar_mode,
    };
    link_send_cfg(&c);
}

/* ---------- 待办持久化 / 回传 ---------- */

static void todos_save(void)
{
    char buf[768];
    int n = 0;
    for (int i = 0; i < s_todos.count; i++) {
        char text[48];
        strlcpy(text, s_todos.items[i].text, sizeof(text));
        for (char *p = text; *p; p++) {
            if (*p == '\n' || *p == '\r' || *p == '\t') {
                *p = ' ';
            }
        }
        int w = snprintf(buf + n, sizeof(buf) - (size_t)n, "%d\t%d\t%s\n",
                         s_todos.items[i].id, s_todos.items[i].done ? 1 : 0, text);
        if (w < 0 || n + w >= (int)sizeof(buf)) {
            break;
        }
        n += w;
    }
    buf[n] = '\0';
    store_set_str(TODO_KEY, buf);
}

static void todos_load(void)
{
    char buf[768];
    if (store_get_str(TODO_KEY, buf, sizeof(buf)) != ESP_OK) {
        return;
    }
    s_todos.count = 0;
    char *line = buf;
    while (*line && s_todos.count < UI_TODO_MAX) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        int id = 0, done = 0;
        char text[48] = { 0 };
        if (sscanf(line, "%d\t%d\t%47[^\n]", &id, &done, text) >= 2) {
            s_todos.items[s_todos.count].id = id;
            s_todos.items[s_todos.count].done = (done != 0);
            strlcpy(s_todos.items[s_todos.count].text, text,
                    sizeof(s_todos.items[0].text));
            s_todos.count++;
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    ui_set_todo(&s_todos);
}

/* 本地改动回传 PC */
static void send_todos_to_pc(void)
{
    todo_list_t t = { 0 };
    t.count = s_todos.count;
    for (int i = 0; i < t.count && i < LINK_TODO_MAX; i++) {
        t.items[i].id = s_todos.items[i].id;
        t.items[i].done = s_todos.items[i].done;
        strlcpy(t.items[i].text, s_todos.items[i].text, sizeof(t.items[i].text));
    }
    link_send_todo(&t);
}

/* 从 RTC 同步日期与时间到 UI（失败则信息条时间留空） */
static void sync_clock_from_rtc(void)
{
    struct tm t;
    if (ds3231_get_time(&t) == ESP_OK) {
        ui_set_date(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_wday);
        ui_set_clock(t.tm_hour, t.tm_min);
    } else {
        ui_set_clock(-1, -1);
    }
}

/* ---------- 渲染 ---------- */

static void present(void)
{
    esp_err_t err = eink_flush_full();
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "flush failed: %s", esp_err_to_name(err));
    }
}

static void render_state(void)
{
    apply_display_modes();
    switch (s_state) {
    case ST_MENU:      ui_render_menu(s_menu_cursor);      break;
    case ST_CONFIG:    ui_render_config(s_cfg_item, s_cfg_value); break;
    case ST_TODO_NAV:  ui_render_todo_nav(s_todo_cursor);  break;
    case ST_TODO_EDIT: ui_render_todo_edit(s_todo_cursor); break;
    case ST_POMO:      ui_render_pomo(s_pomo_left);        break;
    case ST_IDLE:
    default:           ui_render_idle();                   break;
    }
}

static void enter_idle(void)
{
    s_state = ST_IDLE;
    render_state();
    present();
}

static void enter_menu(void)
{
    s_state = ST_MENU;
    s_menu_cursor = 0;
    render_state();
    present();
}

static void enter_config(void)
{
    s_state = ST_CONFIG;
    s_cfg_item = 0;
    s_cfg_value = config_get_value(s_cfg_item);
    s_idle_ticks = 0;
    render_state();
    present();
}

static void enter_pomo(void)
{
    s_state = ST_POMO;
    s_pomo_left = s_cfg.pomo_focus * 60;
    s_pomo_paused = false;
    render_state();
    present();
}

/* ---------- 输入处理（ui-states.md 第四、五节） ---------- */

static void handle_input(input_event_t ev)
{
    switch (s_state) {
    case ST_IDLE:
        if (ev == INPUT_UP || ev == INPUT_DOWN) {
            ui_pool_move(ev == INPUT_UP ? -1 : +1);
            store_set_u32(POOL_KEY_CUR, (uint32_t)ui_pool_current());
            render_state();   /* 整屏重绘（全屏插画模式需重铺整屏） */
            present();
        } else if (ev == INPUT_CENTER_SHORT) {
            s_state = ST_TODO_NAV;
            s_todo_cursor = 0;
            s_idle_ticks = 0;
            render_state();
            present();
        } else if (ev == INPUT_CENTER_LONG) {
            enter_menu();
        }
        break;

    case ST_MENU:
        if (ev == INPUT_UP) {
            s_menu_cursor = (s_menu_cursor + MENU_COUNT - 1) % MENU_COUNT;
            render_state();
            present();
        } else if (ev == INPUT_DOWN) {
            s_menu_cursor = (s_menu_cursor + 1) % MENU_COUNT;
            render_state();
            present();
        } else if (ev == INPUT_CENTER_SHORT) {
            if (s_menu_cursor == MENU_POMO) {
                enter_pomo();
            } else if (s_menu_cursor == MENU_CONFIG) {
                enter_config();
            } else {
                enter_idle();
            }
        } else if (ev == INPUT_CENTER_LONG || ev == INPUT_LEFT || ev == INPUT_RIGHT) {
            enter_idle();
        }
        break;

    case ST_CONFIG:
        if (ev == INPUT_UP) {
            s_cfg_item = (s_cfg_item + UI_CFG_COUNT - 1) % UI_CFG_COUNT;
            s_cfg_value = config_get_value(s_cfg_item);
            render_state();
            present();
        } else if (ev == INPUT_DOWN) {
            s_cfg_item = (s_cfg_item + 1) % UI_CFG_COUNT;
            s_cfg_value = config_get_value(s_cfg_item);
            render_state();
            present();
        } else if (ev == INPUT_LEFT) {
            s_cfg_value = config_cycle(s_cfg_item, s_cfg_value, -1);
            render_state();
            present();
        } else if (ev == INPUT_RIGHT) {
            s_cfg_value = config_cycle(s_cfg_item, s_cfg_value, +1);
            render_state();
            present();
        } else if (ev == INPUT_CENTER_SHORT) {
            config_set_value(s_cfg_item, s_cfg_value);
            config_save_item(s_cfg_item);
            push_cfg();
            render_state();
            present();
        } else if (ev == INPUT_CENTER_LONG) {
            config_set_value(s_cfg_item, s_cfg_value);
            config_save_all();
            push_cfg();
            enter_menu();
        }
        break;

    case ST_TODO_NAV:
        if (ev == INPUT_UP) {
            if (s_todo_cursor > 0) {
                s_todo_cursor--;
            }
            render_state();
            present();
        } else if (ev == INPUT_DOWN) {
            if (s_todo_cursor < s_todos.count - 1) {
                s_todo_cursor++;
            }
            render_state();
            present();
        } else if (ev == INPUT_CENTER_SHORT) {
            if (s_todo_cursor < s_todos.count) {
                s_todos.items[s_todo_cursor].done = !s_todos.items[s_todo_cursor].done;
                ui_set_todo(&s_todos);
                todos_save();
                send_todos_to_pc();
            }
            render_state();
            present();
        } else if (ev == INPUT_CENTER_LONG || ev == INPUT_LEFT) {
            enter_idle();
        }
        break;

    case ST_TODO_EDIT:
        if (ev == INPUT_CENTER_SHORT || ev == INPUT_CENTER_LONG) {
            s_state = ST_TODO_NAV;
            render_state();
            present();
        }
        break;

    case ST_POMO:
        if (ev == INPUT_CENTER_SHORT) {
            s_pomo_paused = !s_pomo_paused;
            render_state();
            present();
        } else if (ev == INPUT_CENTER_LONG) {
            enter_idle();
        }
        break;

    default:
        break;
    }
}

/* ---------- 消息处理 ---------- */

static void handle_msg(const parsed_msg_t *m)
{
    switch (m->type) {
    case MSG_SCHEDULE: {
        ui_schedule_t s = { 0 };
        s.state = m->u.schedule.state;
        s.cur_period = m->u.schedule.cur_period;
        s.cur_left = m->u.schedule.cur_left;
        strlcpy(s.cur_name, m->u.schedule.cur_name, sizeof(s.cur_name));
        strlcpy(s.cur_initial, m->u.schedule.cur_initial, sizeof(s.cur_initial));
        strlcpy(s.cur_start, m->u.schedule.cur_start, sizeof(s.cur_start));
        strlcpy(s.cur_end, m->u.schedule.cur_end, sizeof(s.cur_end));
        strlcpy(s.next_name, m->u.schedule.next_name, sizeof(s.next_name));
        strlcpy(s.next_start, m->u.schedule.next_start, sizeof(s.next_start));
        strlcpy(s.next_end, m->u.schedule.next_end, sizeof(s.next_end));
        ui_set_schedule(&s);
        break;
    }
    case MSG_TODO: {
        memset(&s_todos, 0, sizeof(s_todos));
        s_todos.count = m->u.todo.count;
        for (int i = 0; i < s_todos.count && i < UI_TODO_MAX; i++) {
            s_todos.items[i].id = m->u.todo.items[i].id;
            s_todos.items[i].done = m->u.todo.items[i].done;
            strlcpy(s_todos.items[i].text, m->u.todo.items[i].text,
                    sizeof(s_todos.items[i].text));
        }
        ui_set_todo(&s_todos);
        todos_save();
        break;
    }
    case MSG_QUOTE: {
        ui_pool_set_text(UI_PANEL_QUOTE, "", m->u.quote.text);
        store_set_str(POOL_KEY_QUOTE_TITLE, "");
        store_set_str(POOL_KEY_QUOTE_BODY, m->u.quote.text);
        break;
    }
    case MSG_NEWS: {
        ui_pool_set_text(UI_PANEL_NEWS, m->u.news.title, m->u.news.body);
        store_set_str(POOL_KEY_NEWS_TITLE, m->u.news.title);
        store_set_str(POOL_KEY_NEWS_BODY, m->u.news.body);
        break;
    }
    case MSG_IMAGE: {
        char path[32];
        snprintf(path, sizeof(path), "img_%d.bin", m->u.image.id);
        esp_err_t err = store_write_file(path, m->u.image.data, m->u.image.len);
        ESP_LOGI(TAG, "image %d saved (%s)", m->u.image.id, esp_err_to_name(err));
        if (err == ESP_OK) {
            app_scan_images();   /* 新图入池 */
            render_state();
            present();
        }
        return;   /* 插画不改变当前池游标，只入池 */
    }
    default:
        return;
    }

    render_state();
    present();
}

static void on_link_msg(const parsed_msg_t *msg)
{
    if (s_msgq) {
        /* 拷贝进队列，link_task 立即返回，不阻塞收包 */
        xQueueSend(s_msgq, msg, 0);
    }
}

/* ---------- 任务 ---------- */

static void app_task(void *arg)
{
    (void)arg;
    input_event_t ev;
    parsed_msg_t msg;

    for (;;) {
        bool active = false;

        while (input_poll(&ev)) {
            handle_input(ev);
            active = true;
        }

        while (xQueueReceive(s_msgq, &msg, 0) == pdTRUE) {
            handle_msg(&msg);
            active = true;
        }

        if (active) {
            s_idle_ticks = 0;
        } else if (s_state == ST_CONFIG || s_state == ST_TODO_NAV || s_state == ST_TODO_EDIT) {
            int limit = (s_state == ST_TODO_EDIT) ? TIMEOUT_EDIT : TIMEOUT_NORMAL;
            if (++s_idle_ticks > limit) {
                if (s_state == ST_CONFIG) {
                    config_set_value(s_cfg_item, s_cfg_value);
                    config_save_all();
                    push_cfg();
                }
                enter_idle();
                s_idle_ticks = 0;
            }
        }

        if (++s_power_ticks >= POWER_READ_TICKS) {
            s_power_ticks = 0;
            int mv = 0;
            if (power_read_mv(&mv) == ESP_OK) {
                ESP_LOGI(TAG, "battery %d mV (%d%%)", mv, power_percent(mv));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* ---------- 启动 ---------- */

/* 把 framebuffer 降采样成 ASCII，方便无屏时肉眼验证渲染 */
static void dump_fb_ascii(int cols, int rows)
{
    const uint8_t *fb = eink_framebuffer();
    char line[128];
    ESP_LOGI(TAG, "framebuffer preview (%dx%d):", cols, rows);
    for (int ry = 0; ry < rows; ry++) {
        int y = ry * EINK_HEIGHT / rows;
        int n = 0;
        for (int rx = 0; rx < cols; rx++) {
            int x = rx * EINK_WIDTH / cols;
            bool black = (fb[y * EINK_FB_STRIDE + (x >> 3)] & (0x80 >> (x & 7))) != 0;
            line[n++] = black ? '#' : '.';
        }
        line[n] = '\0';
        ESP_LOGI(TAG, "%s", line);
    }
}

/* 扫 LittleFS 的 img_*.bin，按 id 升序灌入内容池 */
esp_err_t app_scan_images(void)
{
    int ids[32];
    int n = 0;

    DIR *d = opendir("/littlefs");
    if (d == NULL) {
        ESP_LOGW(TAG, "open /littlefs failed");
        ui_pool_set_images(NULL, 0);
        return ESP_FAIL;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < (int)(sizeof(ids) / sizeof(ids[0]))) {
        int id;
        if (sscanf(e->d_name, "img_%d.bin", &id) == 1) {
            ids[n++] = id;
        }
    }
    closedir(d);

    /* 插入排序，升序 */
    for (int i = 1; i < n; i++) {
        int k = ids[i], j = i - 1;
        while (j >= 0 && ids[j] > k) {
            ids[j + 1] = ids[j];
            j--;
        }
        ids[j + 1] = k;
    }

    ui_pool_set_images(ids, n);
    ESP_LOGI(TAG, "image pool: %d file(s)", n);
    return ESP_OK;
}

/* 从 NVS 恢复新闻 / 一言文本项（以 body 为准判断是否存在） */
static void load_pool_texts(void)
{
    char title[24];
    char body[256];

    body[0] = '\0';
    if (store_get_str(POOL_KEY_NEWS_BODY, body, sizeof(body)) == ESP_OK) {
        title[0] = '\0';
        store_get_str(POOL_KEY_NEWS_TITLE, title, sizeof(title));
        ui_pool_set_text(UI_PANEL_NEWS, title, body);
    }
    body[0] = '\0';
    if (store_get_str(POOL_KEY_QUOTE_BODY, body, sizeof(body)) == ESP_OK) {
        title[0] = '\0';
        store_get_str(POOL_KEY_QUOTE_TITLE, title, sizeof(title));
        ui_pool_set_text(UI_PANEL_QUOTE, title, body);
    }
}

/* RTC 唤醒：自动轮换（仅当 cfg_rotate 开启） */
void app_on_rtc_wakeup(void)
{
    if (s_cfg.rotate) {
        ui_pool_move(+1);
        store_set_u32(POOL_KEY_CUR, (uint32_t)ui_pool_current());
    }
    render_state();
    present();
}

/* 暂无真实数据源时的占位内容，便于无硬件时验证渲染 */
static void seed_placeholder_data(void)
{
    ui_schedule_t s = {
        .state = 1,
        .cur_period = 3,
        .cur_name = "数学",
        .cur_initial = "数",
        .cur_start = "09:40",
        .cur_end = "10:20",
        .cur_left = 23,
        .next_name = "英语",
        .next_start = "10:30",
        .next_end = "11:10",
    };
    ui_set_schedule(&s);

    ui_todo_list_t t = { .count = 3 };
    t.items[0] = (ui_todo_item_t){ .id = 1, .done = true,  .text = "英语单词 Unit5" };
    t.items[1] = (ui_todo_item_t){ .id = 2, .done = false, .text = "买电池" };
    t.items[2] = (ui_todo_item_t){ .id = 3, .done = false, .text = "预习生物第3章" };
    ui_set_todo(&t);

    ui_set_date(2026, 9, 17, 4);   /* 周四 */

    /* 无任何真实内容时给个占位，便于无硬件时验证 */
    if (ui_pool_count() == 0) {
        ui_pool_set_text(UI_PANEL_QUOTE, "", "把一件事做完整，比做十件半成品更有价值。");
    }
}

esp_err_t app_start(void)
{
    s_msgq = xQueueCreate(4, sizeof(parsed_msg_t));
    if (s_msgq == NULL) {
        return ESP_ERR_NO_MEM;
    }

    config_load();
    apply_display_modes();
#if !CONFIG_APP_SKIP_INPUT
    input_init();
#endif

    link_set_rx_callback(on_link_msg);
    esp_err_t err = link_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "link_init failed: %s", esp_err_to_name(err));
    } else {
        /* 上电握手，紧跟着把设备当前配置报给 PC */
        link_send_hello(APP_FW_VER);
        push_cfg();
    }

    load_pool_texts();
    todos_load();
    sync_clock_from_rtc();

    uint32_t cur = 0;
    if (store_get_u32(POOL_KEY_CUR, &cur) == ESP_OK) {
        ui_pool_set_current((int)cur);
    }

    seed_placeholder_data();

    s_state = ST_IDLE;
    s_idle_ticks = 0;
    render_state();
    present();
    dump_fb_ascii(100, 30);

#if CONFIG_APP_QEMU_TOUR
    /* 仿真巡视：在 app_task 起来之前跑完，避免并发重绘 */
    {
        extern void qemu_tour_run(void);
        qemu_tour_run();
    }
#endif

    if (xTaskCreate(app_task, "app_task", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "app started (state=IDLE)");
    return ESP_OK;
}
