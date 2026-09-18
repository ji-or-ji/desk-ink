#include "demo.h"

#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "eink.h"
#include "store.h"
#include "demo_source.h"

/*
 * Bad Apple 播放器
 *
 * 读 BAF1 帧包（文件源）-> PackBits 解压 -> 写 framebuffer -> 刷新。
 * 首帧走整屏刷新建立基线，其余按 dirty 区域走局部刷新。
 * 局部刷新在 QEMU（CONFIG_APP_SKIP_EINK）下不可用，播放逻辑照跑，便于验证解码与 blit。
 */

static const char *TAG = "demo";

#ifndef CONFIG_DEMO_BADAPP_BUNDLE_PATH
#define CONFIG_DEMO_BADAPP_BUNDLE_PATH "/littlefs/badapple.baf"
#endif
#ifndef CONFIG_DEMO_BADAPP_LOOPS
#define CONFIG_DEMO_BADAPP_LOOPS 0
#endif
#ifndef CONFIG_DEMO_MAX_FRAMES
#define CONFIG_DEMO_MAX_FRAMES 0
#endif
#ifndef CONFIG_DEMO_DUMP_EVERY
#define CONFIG_DEMO_DUMP_EVERY 0
#endif

#if defined(CONFIG_DEMO_EMBED_BUNDLE) && defined(DEMO_HAS_BUNDLE)
extern const uint8_t badapple_baf_start[] asm("_binary_badapple_baf_start");
extern const uint8_t badapple_baf_end[]   asm("_binary_badapple_baf_end");

static void embed_bundle_once(const char *path)
{
    FILE *existing = fopen(path, "rb");
    if (existing) {
        fclose(existing);
        ESP_LOGI(TAG, "bundle already on LittleFS: %s", path);
        return;
    }
    size_t len = (size_t)(badapple_baf_end - badapple_baf_start);
    esp_err_t err = store_write_file("badapple.baf", badapple_baf_start, len);
    ESP_LOGI(TAG, "embed bundle: %u bytes -> %s", (unsigned)len, esp_err_to_name(err));
}
#else
static void embed_bundle_once(const char *path)
{
    (void)path;
}
#endif

#if CONFIG_DEMO_DUMP_EVERY > 0
/* 与 app/qemu_tour.c 的 FB64 格式一致，主机 tools/fb64-decode.ps1 可还原 */
static void dump_fb_b64(const char *label)
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
#endif

static esp_err_t play_once(demo_source_t *src, uint8_t *packed)
{
    uint32_t total = src->frame_count;
#if CONFIG_DEMO_MAX_FRAMES > 0
    if (total > (uint32_t)CONFIG_DEMO_MAX_FRAMES) {
        total = (uint32_t)CONFIG_DEMO_MAX_FRAMES;
    }
#endif
    int period_ms = (src->fps > 0) ? (1000 / src->fps) : 500;
    bool flush_warned = false;

    for (uint32_t i = 0; i < total; i++) {
        int64_t t0 = esp_timer_get_time();

        uint16_t dirty[4] = { 0, 0, 0, 0 };
        size_t packed_len = 0;
        esp_err_t err = src->read_frame(src, i, packed, (size_t)src->stride * src->height,
                                        &packed_len, dirty);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "read frame %u failed: %s", (unsigned)i, esp_err_to_name(err));
            return err;
        }

        memcpy(eink_framebuffer(), packed, packed_len);

        esp_err_t ferr;
        bool first = (i == 0);
        bool no_change = (dirty[2] == 0 || dirty[3] == 0);
        if (first) {
            ferr = eink_flush_full();
        } else if (no_change) {
            ferr = ESP_OK;
        } else {
            ferr = eink_flush_partial(dirty[0], dirty[1], dirty[2], dirty[3]);
        }
        if (ferr == ESP_ERR_NOT_SUPPORTED) {
            if (!flush_warned) {
                ESP_LOGW(TAG, "flush unavailable (QEMU?); decoding only");
                flush_warned = true;
            }
        } else if (ferr != ESP_OK) {
            ESP_LOGW(TAG, "frame %u flush failed: %s", (unsigned)i, esp_err_to_name(ferr));
        }

#if CONFIG_DEMO_DUMP_EVERY > 0
        if ((i % CONFIG_DEMO_DUMP_EVERY) == 0) {
            char label[32];
            snprintf(label, sizeof(label), "demo-%u", (unsigned)i);
            dump_fb_b64(label);
        }
#endif

        int elapsed_ms = (int)((esp_timer_get_time() - t0) / 1000);
        int wait_ms = period_ms - elapsed_ms;
        if (wait_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        }
    }
    return ESP_OK;
}

esp_err_t demo_badapple_start(void)
{
    const char *path = CONFIG_DEMO_BADAPP_BUNDLE_PATH;
    embed_bundle_once(path);

    demo_source_t *src = demo_source_file();
    esp_err_t err = src->open(src, path);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "demo source open failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t *packed = malloc((size_t)src->stride * src->height);
    if (packed == NULL) {
        src->close(src);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Bad Apple start: %u frame(s) @ %u fps, loops=%d",
             (unsigned)src->frame_count, src->fps, CONFIG_DEMO_BADAPP_LOOPS);

    uint32_t loops = CONFIG_DEMO_BADAPP_LOOPS;
    esp_err_t result = ESP_OK;
    for (uint32_t pass = 0; loops == 0 || pass < loops; pass++) {
        result = play_once(src, packed);
        if (result != ESP_OK) {
            break;
        }
    }

    free(packed);
    src->close(src);
    ESP_LOGI(TAG, "Bad Apple done (%s)", esp_err_to_name(result));
    return result;
}
