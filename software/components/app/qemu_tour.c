#include "sdkconfig.h"

#if CONFIG_APP_QEMU_TOUR || CONFIG_APP_QEMU_DUMP_B64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "eink.h"
#include "ui.h"
#include "store.h"

/*
 * QEMU 状态巡视（仅仿真构建启用）
 *
 * - qemu_dump_fb_b64(): 把 framebuffer 以 base64 打到串口
 * - qemu_tour_run():    依次渲染各 UI 状态，每步 dump 一次
 *
 * 只调用 ui.h 的公开 API，不碰 app.c 内部状态。
 */

static const char *TAG = "tour";

#ifndef APP_TOUR_HAS_PHOTO
#define APP_TOUR_HAS_PHOTO 0   /* 由组件 CMake 在 testdata 存在时置 1 */
#endif

/* ---------- base64 dump ---------- */

void qemu_dump_fb_b64(const char *label)
{
    const uint8_t *fb = eink_framebuffer();
    size_t len = EINK_FB_SIZE;
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    printf("FB64 BEGIN %u %s\n", (unsigned)len, label ? label : "");

    char line[204];
    int li = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)fb[i] << 16;
        int rem = (int)(len - i);
        if (rem > 1) n |= (uint32_t)fb[i + 1] << 8;
        if (rem > 2) n |= (uint32_t)fb[i + 2];

        line[li++] = b64[(n >> 18) & 0x3F];
        line[li++] = b64[(n >> 12) & 0x3F];
        line[li++] = (rem > 1) ? b64[(n >> 6) & 0x3F] : '=';
        line[li++] = (rem > 2) ? b64[n & 0x3F] : '=';
        if (li >= 200) {
            line[li] = '\0';
            printf("%s\n", line);
            li = 0;
        }
    }
    if (li > 0) {
        line[li] = '\0';
        printf("%s\n", line);
    }
    printf("FB64 END\n");
    fflush(stdout);
}

#if CONFIG_APP_QEMU_TOUR

/* ---------- 合成插画（无真实照片时的回退） ---------- */
#if !APP_TOUR_HAS_PHOTO

static const int s_bayer[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

static void set_bit(uint8_t *buf, int stride, int x, int y, bool black)
{
    uint8_t *p = &buf[y * stride + (x >> 3)];
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (black) {
        *p |= m;
    } else {
        *p &= (uint8_t)~m;
    }
}

/* 渐变天空 + 太阳 + 两座山 + 地面，4×4 Bayer 抖动 */
static void make_scene(uint8_t *buf, int w, int h, int stride)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int v = 238 - (y * 52) / 100;
            int dx = x - (w * 79) / 100;
            int dy = y - (h * 22) / 100;
            int r2 = dx * dx + dy * dy;
            if (r2 < 28 * 28) {
                v = 252;
            } else if (r2 < 34 * 34) {
                v = 205;
            }
            int m1 = (h * 44) / 100 - (abs(x - (w * 27) / 100) * 72) / 100;
            if (y > m1) {
                v = 88;
            }
            int m2 = (h * 59) / 100 - (abs(x - (w * 71) / 100) * 55) / 100;
            if (y > m2) {
                v = 58;
            }
            if (y > (h * 79) / 100) {
                v = 44 + (x * 5) % 26;
            }
            int t = s_bayer[y % 4][x % 4];
            set_bit(buf, stride, x, y, (v + t / 5 - 50) < 128);
        }
    }
}

#endif /* !APP_TOUR_HAS_PHOTO */

#if APP_TOUR_HAS_PHOTO
/* 由 target_add_binary_data 生成（software/testdata/art_full.bin / art_panel.bin） */
extern const uint8_t art_full_bin_start[]  asm("_binary_art_full_bin_start");
extern const uint8_t art_full_bin_end[]    asm("_binary_art_full_bin_end");
extern const uint8_t art_panel_bin_start[] asm("_binary_art_panel_bin_start");
extern const uint8_t art_panel_bin_end[]   asm("_binary_art_panel_bin_end");
#endif

static void write_tour_images(void)
{
#if APP_TOUR_HAS_PHOTO
    size_t flen = (size_t)(art_full_bin_end - art_full_bin_start);
    size_t plen = (size_t)(art_panel_bin_end - art_panel_bin_start);
    esp_err_t e1 = store_write_file("img_1.bin", art_full_bin_start, flen);
    esp_err_t e2 = store_write_file("img_2.bin", art_panel_bin_start, plen);
    ESP_LOGI(TAG, "photo images: img_1=%s (%u B) img_2=%s (%u B)",
             esp_err_to_name(e1), (unsigned)flen, esp_err_to_name(e2), (unsigned)plen);
#else
    static uint8_t full[EINK_FB_SIZE];                 /* 400×300 */
    static uint8_t panel[((134 + 7) / 8) * 200];       /* 134×200 = 3400 */

    memset(full, 0, sizeof(full));
    memset(panel, 0, sizeof(panel));
    make_scene(full, 400, 300, 50);
    make_scene(panel, 134, 200, 17);

    esp_err_t e1 = store_write_file("img_1.bin", full, sizeof(full));
    esp_err_t e2 = store_write_file("img_2.bin", panel, sizeof(panel));
    ESP_LOGI(TAG, "synth images: img_1=%s img_2=%s", esp_err_to_name(e1), esp_err_to_name(e2));
#endif
}

/* ---------- 巡视数据 ---------- */

static void setup_pool(void)
{
    ui_pool_clear();
    ui_pool_set_text(UI_PANEL_NEWS, "示例新闻",
                     "这是一条用于巡视排版的新闻摘要，用来检查小窗正文换行与禁则。");
    ui_pool_set_text(UI_PANEL_QUOTE, "",
                     "把一件事做完整，比做十件半成品更有价值。");
    static const int ids[] = { 1, 2 };
    ui_pool_set_images(ids, 2);
}

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

static void step(const char *label)
{
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "step: %s", label);
    qemu_dump_fb_b64(label);
}

void qemu_tour_run(void)
{
    ESP_LOGI(TAG, "QEMU tour start");
    write_tour_images();
    setup_data();
    setup_pool();

    /* 1. IDLE 每日一言 */
    ui_set_art_mode(0);
    ui_set_bar_mode(UI_BAR_SOLID);
    ui_pool_set_current(1);
    ui_render_idle();
    step("IDLE-quote");

    /* 2. IDLE 新闻 */
    ui_pool_set_current(0);
    ui_render_idle();
    step("IDLE-news");

    /* 3. IDLE 插画进小窗 */
    ui_set_art_mode(1);
    ui_pool_set_current(2);
    ui_render_idle();
    step("IDLE-art-panel");

    /* 4-6. 全屏插画 + 信息条三模式 */
    ui_set_art_mode(0);
    ui_set_bar_mode(UI_BAR_SOLID);
    ui_render_idle();
    step("FULL-bar-solid");

    ui_set_bar_mode(UI_BAR_INVERT);
    ui_render_idle();
    step("FULL-bar-invert");

    ui_set_bar_mode(UI_BAR_INVERT_AUTO);
    ui_render_idle();
    step("FULL-bar-auto");

    /* 7. 内容池为空 */
    ui_pool_clear();
    ui_set_art_mode(0);
    ui_render_idle();
    step("IDLE-empty");

    /* 8. MENU 光标停在「配置」 */
    setup_pool();
    ui_render_menu(1);
    step("MENU");

    /* 9-14. CONFIG 六项各停一次 */
    static const int items[UI_CFG_COUNT] = {
        UI_CFG_REFRESH, UI_CFG_POMO_FOCUS, UI_CFG_POMO_BREAK,
        UI_CFG_ROTATE, UI_CFG_ART_MODE, UI_CFG_BAR_MODE
    };
    static const int vals[UI_CFG_COUNT] = { 1, 25, 5, 1, 0, 2 };
    static const char *const names[UI_CFG_COUNT] = {
        "refresh", "pomo-focus", "pomo-break", "rotate", "art-mode", "bar-mode"
    };
    for (int i = 0; i < UI_CFG_COUNT; i++) {
        char label[32];
        snprintf(label, sizeof(label), "CONFIG-%s", names[i]);
        ui_render_config(items[i], vals[i]);
        step(label);
    }

    /* 15-17. 待办 / 编辑 / 番茄钟 */
    setup_pool();
    ui_pool_set_current(1);
    ui_render_idle();
    ui_render_todo_nav(1);
    step("TODO_NAV");

    ui_render_idle();
    ui_render_todo_edit(2);
    step("TODO_EDIT");

    ui_render_idle();
    ui_render_pomo(1477);
    step("POMO");

    ESP_LOGI(TAG, "QEMU tour done");
}

#endif /* CONFIG_APP_QEMU_TOUR */

#endif /* CONFIG_APP_QEMU_TOUR || CONFIG_APP_QEMU_DUMP_B64 */
