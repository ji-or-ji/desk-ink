#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * font — 点阵字库
 *
 * 汉字 16×16（32 字节/字），ASCII 8×16（16 字节/字）。
 * 字库格式见 software/font/README.md：
 *   font16_simsun.bin: magic 'F16B' + uint16 N + uint16 codes[N] + uint8 bitmaps[N][32]
 *   ascii8x16.bin    : 96 字符 × 16 字节，0x20~0x7F，MSB 在左
 */

esp_err_t font_init(const uint8_t *cjk, size_t cjk_len,
                    const uint8_t *ascii, size_t ascii_len);

/* 取字模：返回指向点阵的指针；未收录返回 NULL。
 * ASCII 返回 16 字节（8×16），汉字返回 32 字节（16×16）。 */
const uint8_t *font_get_glyph(uint32_t codepoint);

bool font_is_ascii(uint32_t codepoint);
int  font_glyph_width(uint32_t codepoint);   /* 8 或 16 */

/* 画一行 UTF-8 文本到 framebuffer；返回占用宽度（像素）。
 * invert=true 时该字格反白（黑底白字）。 */
int font_draw_text(int x, int y, const char *utf8, bool invert);
int font_text_width(const char *utf8);
