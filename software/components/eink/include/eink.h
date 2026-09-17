#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * eink — 墨水屏（SSD1680）
 *
 * 第一阶段只实现 framebuffer 与绘制原语，SPI 初始化/刷新留到第二阶段。
 * framebuffer 约定：1 bit/像素，1 = 黑，0 = 白，行优先，每行 50 字节。
 */

#define EINK_WIDTH      400
#define EINK_HEIGHT     300
#define EINK_FB_STRIDE  (EINK_WIDTH / 8)              /* 50 */
#define EINK_FB_SIZE    (EINK_FB_STRIDE * EINK_HEIGHT) /* 15000 */

esp_err_t eink_init(void);

/* 直接访问 framebuffer（供 ui / 调试用） */
uint8_t *eink_framebuffer(void);

/* ---------- framebuffer 绘制原语 ---------- */
void fb_clear(bool white);
void fb_set_pixel(int x, int y, bool black);
void fb_draw_hline(int x0, int x1, int y, bool black);
void fb_draw_vline(int x, int y0, int y1, bool black);
void fb_draw_rect(int x, int y, int w, int h, bool black, bool fill);
void fb_blit_1bit(const uint8_t *data, int x, int y, int w, int h, bool invert);

/* ---------- 刷新（第二阶段实现） ---------- */
esp_err_t eink_flush_full(void);
esp_err_t eink_flush_partial(int x, int y, int w, int h);
