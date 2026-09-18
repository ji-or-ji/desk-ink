/*
 * qemu_tour — 仿真巡视：把各个界面状态自动走一遍并导出 framebuffer
 *
 * 只在 CONFIG_APP_QEMU_TOUR=y 时生效（默认 n，正常运行完全不受影响）。
 *
 * 目的：板子到手前，用**真 C 代码**验证 UI 渲染，而不是靠复刻。
 * 只调用 ui.h 的公开 API，不依赖 app.c 的内部状态。
 */

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#if CONFIG_APP_QEMU_TOUR

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "eink.h"
#include "ui.h"
#include "store.h"

static const char *TAG = "tour";

/* ---------------- 合成插画：写进 LittleFS，走真实读取路径 ---------------- */

#define SYNTH_MAX (EINK_FB_STRIDE * EINK_HEIGHT)   /* 15000 */

static void make_synth_image(int id, int w, int h)
{
    static uint8_t buf[SYNTH_MAX];
    static const int bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };

    if (w <= 0 || h <= 0) {
        return;
    }
    int stride = (w + 7) / 8;
    if (stride * h > SYNTH_MAX) {
        return;
    }
    memset(buf, 0, sizeof(buf));

    /* 亮天空 + 太阳 + 远山 + 地面；层次多一些，便于看二值化效果 */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int v = 225 - (y * 220) / h;

            int cx = (w * 72) / 100, cy = (h * 22) / 100;
            int dx = x - cx, dy = y - cy;
            int rr = (w * 12) / 100;
            if (dx * dx + dy * dy < rr * rr) {
                v = 250;
            }

            int q = w / 4;
            int m1 = (h * 52) / 100 - ((x > q ? x - q : q - x) * 50) / 100;
            if (y > m1) {
                v = 95;
            }
            if (y > (h * 74) / 100) {
                v = 60 + ((x * 5) % 28);
            }

            int t = bayer[y & 3][x & 3];
            if (v < (t * 2 + 1) * 8) {
                buf[y * stride + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
            }
        }
    }

    char path[32];
    snprintf(path, sizeof(path), "img_%d.bin", id);
    esp_err_t err = store_write_file(path, buf, (size_t)(stride * h));
    ESP_LOGI(TAG, "synth image %s: %d bytes, %s", path, stride * h,
             err == ESP_OK ? "ok" : esp_err_to_name(err));
}

/* ---------------- framebuffer 导出 ---------------- */

static void dump_b64(const char *label)
{
#if CONFIG_APP_QEMU_DUMP_B64
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *fb = eink_framebuffer();
    const int len = EINK_FB_SIZE;
    int col = 0;

    printf("FB64 BEGIN %s %d\n", label, len);
    for (int i = 0; i < len; i += 3) {
        int rem = len - i;
        uint32_t n = (uint32_t)fb[i] << 16;
        if (rem > 1) {
            n |= (uint32_t)fb[i + 1] << 8;
        }
        if (rem > 2) {
            n |= (uint32_t)fb[i + 2];
        }
        char o[4];
        o[0] = tbl[(n >> 18) & 63];
        o[1] = tbl[(n >> 12) & 63];
        o[2] = (rem > 1) ? tbl[(n >> 6) & 63] : '=';
        o[3] = (rem > 2) ? tbl[n & 63] : '=';
        for (int k = 0; k < 4; k++) {
            putchar(o[k]);
            if (++col >= 200) {
                putchar('\n');
                col = 0;
            }
        }
    }
    if (col) {
        putchar('\n');
    }
    printf("FB64 END %s\n", label);
    fflush(stdout);
#else
    ESP_LOGI(TAG, "step %s (dump disabled)", label);
#endif
}

static void step(const char *label)
{
    dump_b64(label);
    vTaskDelay(pdMS_TO_TICKS(200));
}

/* ---------------- 巡视数据 ---------------- */

static void setup_data(void)
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
    ui_set_clock(9, 40);
}

/* ---------------- 巡视主体 ---------------- */

void qemu_tour_run(void)
{
    ESP_LOGI(TAG, "=== QEMU TOUR begin ===");

    make_synth_image(1, 400, 300);   /* 全屏尺寸 */
    make_synth_image(2, 134, 200);   /* 小窗尺寸 */

    setup_data();
    ui_set_art_mode(0);
    ui_set_bar_mode(UI_BAR_SOLID);

    /* ---- 文本项 ---- */
    ui_pool_clear();
    ui_pool_set_text(UI_PANEL_QUOTE, "", "把一件事做完整，比做十件半成品更有价值。");
    ui_pool_set_current(0);
    ui_render_idle();
    step("01_idle_quote");

    ui_pool_set_text(UI_PANEL_NEWS, "校内通知",
                     "下周三下午进行秋季运动会彩排，请各班提前十分钟到操场集合，注意携带水杯。");
    ui_pool_set_current(0);   /* news 在池里排第 0 */
    ui_render_idle();
    step("02_idle_news");

    /* ---- 菜单与配置页 ---- */
    ui_render_menu(1);
    step("03_menu");

    ui_render_config(UI_CFG_REFRESH, 1);
    step("04_config_refresh");

    ui_render_config(UI_CFG_POMO_FOCUS, 25);
    step("05_config_pomo_focus");

    ui_render_config(UI_CFG_BAR_MODE, 2);
    step("06_config_bar_mode");

    /* ---- 待办与番茄钟 ---- */
    ui_render_idle();
    ui_render_todo_nav(1);
    step("07_todo_nav");

    ui_render_todo_edit(2);
    step("08_todo_edit");

    ui_render_idle();
    ui_render_pomo(1477);
    step("09_pomo");

    /* ---- 全屏插画 × 三种信息条 ---- */
    int full_ids[1] = { 1 };
    ui_pool_clear();
    ui_pool_set_images(full_ids, 1);
    ui_pool_set_current(0);
    ui_set_art_mode(0);

    ui_set_bar_mode(UI_BAR_SOLID);
    ui_render_idle();
    step("10_full_solid");

    ui_set_bar_mode(UI_BAR_INVERT);
    ui_render_idle();
    step("11_full_invert");

    ui_set_bar_mode(UI_BAR_INVERT_AUTO);
    ui_render_idle();
    step("12_full_auto");

    /* ---- 插画进小窗 ---- */
    int panel_ids[1] = { 2 };
    ui_pool_clear();
    ui_pool_set_images(panel_ids, 1);
    ui_pool_set_current(0);
    ui_set_art_mode(1);
    ui_render_idle();
    step("13_panel_image");

    ESP_LOGI(TAG, "=== QEMU TOUR end ===");
}

#else  /* !CONFIG_APP_QEMU_TOUR */

void qemu_tour_run(void)
{
}

#endif
