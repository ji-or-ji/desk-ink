#include "eink.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "eink";

static uint8_t s_fb[EINK_FB_SIZE];

uint8_t *eink_framebuffer(void)
{
    return s_fb;
}

esp_err_t eink_init(void)
{
    fb_clear(true);
    ESP_LOGI(TAG, "framebuffer ready (%dx%d, %d bytes)", EINK_WIDTH, EINK_HEIGHT, EINK_FB_SIZE);
    return ESP_OK;
}

void fb_clear(bool white)
{
    memset(s_fb, white ? 0x00 : 0xFF, sizeof(s_fb));
}

void fb_set_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= EINK_WIDTH || y < 0 || y >= EINK_HEIGHT) {
        return;
    }
    uint8_t *byte = &s_fb[y * EINK_FB_STRIDE + (x >> 3)];
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    if (black) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

void fb_draw_hline(int x0, int x1, int y, bool black)
{
    if (x0 > x1) {
        int t = x0; x0 = x1; x1 = t;
    }
    for (int x = x0; x <= x1; x++) {
        fb_set_pixel(x, y, black);
    }
}

void fb_draw_vline(int x, int y0, int y1, bool black)
{
    if (y0 > y1) {
        int t = y0; y0 = y1; y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        fb_set_pixel(x, y, black);
    }
}

void fb_draw_rect(int x, int y, int w, int h, bool black, bool fill)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (fill) {
        for (int yy = y; yy <= y1; yy++) {
            fb_draw_hline(x, x1, yy, black);
        }
        return;
    }
    fb_draw_hline(x, x1, y, black);
    fb_draw_hline(x, x1, y1, black);
    fb_draw_vline(x, y, y1, black);
    fb_draw_vline(x1, y, y1, black);
}

void fb_blit_1bit(const uint8_t *data, int x, int y, int w, int h, bool invert)
{
    if (data == NULL || w <= 0 || h <= 0) {
        return;
    }
    int stride = (w + 7) / 8;
    for (int ry = 0; ry < h; ry++) {
        for (int rx = 0; rx < w; rx++) {
            uint8_t bit = (data[ry * stride + (rx >> 3)] >> (7 - (rx & 7))) & 1;
            if (invert) {
                bit ^= 1;
            }
            fb_set_pixel(x + rx, y + ry, bit != 0);
        }
    }
}

esp_err_t eink_flush_full(void)
{
    /* 第二阶段：SSD1680 整屏刷新 */
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t eink_flush_partial(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
    /* 第二阶段：SSD1680 局部刷新（独立 LUT） */
    return ESP_ERR_NOT_SUPPORTED;
}
