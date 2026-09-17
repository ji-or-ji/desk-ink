#include "app.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "eink.h"
#include "ui.h"
#include "input.h"
#include "link.h"
#include "store.h"

static const char *TAG = "app";

/* ---------- 配置项（NVS namespace "deskink"，见 变更-配置页.md） ---------- */
#define CFG_KEY_REFRESH    "cfg_refresh"
#define CFG_KEY_POMO_FOCUS "cfg_pomo_focus"
#define CFG_KEY_POMO_BREAK "cfg_pomo_break"
#define CFG_KEY_ROTATE     "cfg_rotate"

#define CFG_REFRESH_DEFAULT 1     /* 常规 */
#define CFG_FOCUS_DEFAULT   25
#define CFG_BREAK_DEFAULT   5
#define CFG_ROTATE_DEFAULT  1     /* 开 */

static const int REFRESH_VALS[] = { 0, 1, 2 };        /* 自动 / 常规 / 静默 */
static const int FOCUS_VALS[]   = { 15, 25, 45 };
static const int BREAK_VALS[]   = { 5, 10 };
static const int ROTATE_VALS[]  = { 0, 1 };

typedef struct {
    int refresh;
    int pomo_focus;
    int pomo_break;
    int rotate;
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

static app_config_t   s_cfg;
static app_state_t    s_state;
static int            s_menu_cursor;
static int            s_cfg_item;
static int            s_cfg_value;
static int            s_todo_cursor;
static int            s_pomo_left;
static bool           s_pomo_paused;
static int            s_idle_ticks;
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
}

static const char *config_key(int item)
{
    switch (item) {
    case UI_CFG_REFRESH:    return CFG_KEY_REFRESH;
    case UI_CFG_POMO_FOCUS: return CFG_KEY_POMO_FOCUS;
    case UI_CFG_POMO_BREAK: return CFG_KEY_POMO_BREAK;
    case UI_CFG_ROTATE:     return CFG_KEY_ROTATE;
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
            /* 小窗轮换：具体内容切换由数据层处理，这里先重绘小窗 */
            ui_redraw_panel();
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
            render_state();
            present();
        } else if (ev == INPUT_CENTER_LONG) {
            config_set_value(s_cfg_item, s_cfg_value);
            config_save_all();
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
            if (s_todo_cursor < UI_TODO_MAX - 1) {
                s_todo_cursor++;
            }
            render_state();
            present();
        } else if (ev == INPUT_CENTER_SHORT) {
            /* 切换勾选 → 回传 PC（数据层实现后接 link_send_todo） */
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
        ui_todo_list_t t = { 0 };
        t.count = m->u.todo.count;
        for (int i = 0; i < t.count && i < UI_TODO_MAX; i++) {
            t.items[i].id = m->u.todo.items[i].id;
            t.items[i].done = m->u.todo.items[i].done;
            strlcpy(t.items[i].text, m->u.todo.items[i].text, sizeof(t.items[i].text));
        }
        ui_set_todo(&t);
        break;
    }
    case MSG_QUOTE: {
        ui_panel_t p = { 0 };
        p.kind = UI_PANEL_QUOTE;
        strlcpy(p.body, m->u.quote.text, sizeof(p.body));
        ui_set_panel(&p);
        break;
    }
    case MSG_NEWS: {
        ui_panel_t p = { 0 };
        p.kind = UI_PANEL_NEWS;
        strlcpy(p.title, m->u.news.title, sizeof(p.title));
        strlcpy(p.body, m->u.news.body, sizeof(p.body));
        ui_set_panel(&p);
        break;
    }
    case MSG_IMAGE: {
        char path[32];
        snprintf(path, sizeof(path), "img_%d.bin", m->u.image.id);
        esp_err_t err = store_write_file(path, m->u.image.data, m->u.image.len);
        ESP_LOGI(TAG, "image %d saved (%s)", m->u.image.id, esp_err_to_name(err));
        return;   /* 插画不直接改变当前画面 */
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
                }
                enter_idle();
                s_idle_ticks = 0;
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

    ui_panel_t p = {
        .kind = UI_PANEL_QUOTE,
        .body = "把一件事做完整，比做十件半成品更有价值。",
    };
    ui_set_panel(&p);
    ui_set_date(2026, 9, 17, 4);   /* 周四 */
}

esp_err_t app_start(void)
{
    s_msgq = xQueueCreate(4, sizeof(parsed_msg_t));
    if (s_msgq == NULL) {
        return ESP_ERR_NO_MEM;
    }

    config_load();
    input_init();

    link_set_rx_callback(on_link_msg);
    esp_err_t err = link_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "link_init failed: %s", esp_err_to_name(err));
    }

    seed_placeholder_data();

    s_state = ST_IDLE;
    s_idle_ticks = 0;
    render_state();
    present();
    dump_fb_ascii(100, 30);

    if (xTaskCreate(app_task, "app_task", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "app started (state=IDLE)");
    return ESP_OK;
}
