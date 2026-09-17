#include "font.h"

#include <string.h>
#include "eink.h"
#include "esp_log.h"

static const char *TAG = "font";

#define CJK_MAGIC     0x42363146u   /* 'F16B' little-endian */
#define CJK_GLYPH_LEN 32
#define ASCII_GLYPH_LEN 16
#define ASCII_FIRST   0x20
#define ASCII_LAST    0x7F
#define ASCII_COUNT   (ASCII_LAST - ASCII_FIRST + 1)

static const uint8_t *s_cjk;
static uint16_t       s_cjk_count;
static const uint8_t *s_cjk_codes;
static const uint8_t *s_cjk_bitmaps;

static const uint8_t *s_ascii;
static size_t         s_ascii_len;

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

esp_err_t font_init(const uint8_t *cjk, size_t cjk_len,
                    const uint8_t *ascii, size_t ascii_len)
{
    if (cjk == NULL || ascii == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cjk_len < 6 || memcmp(cjk, "F16B", 4) != 0) {
        ESP_LOGE(TAG, "CJK font magic mismatch");
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t n = rd_u16(cjk + 4);
    size_t need = 6 + (size_t)n * 2 + (size_t)n * CJK_GLYPH_LEN;
    if (cjk_len < need) {
        ESP_LOGE(TAG, "CJK font truncated: need %u, have %u", (unsigned)need, (unsigned)cjk_len);
        return ESP_ERR_INVALID_SIZE;
    }
    if (ascii_len < (size_t)ASCII_COUNT * ASCII_GLYPH_LEN) {
        ESP_LOGE(TAG, "ASCII font truncated: have %u", (unsigned)ascii_len);
        return ESP_ERR_INVALID_SIZE;
    }

    s_cjk         = cjk;
    s_cjk_count   = n;
    s_cjk_codes   = cjk + 6;
    s_cjk_bitmaps = cjk + 6 + (size_t)n * 2;
    s_ascii       = ascii;
    s_ascii_len   = ascii_len;

    ESP_LOGI(TAG, "loaded %u CJK glyphs + %d ASCII glyphs",
             (unsigned)n, ASCII_COUNT);
    return ESP_OK;
}

bool font_is_ascii(uint32_t codepoint)
{
    return codepoint < 0x80;
}

int font_glyph_width(uint32_t codepoint)
{
    return font_is_ascii(codepoint) ? 8 : 16;
}

const uint8_t *font_get_glyph(uint32_t codepoint)
{
    if (font_is_ascii(codepoint)) {
        if (codepoint < ASCII_FIRST || codepoint > ASCII_LAST) {
            return NULL;
        }
        size_t off = (size_t)(codepoint - ASCII_FIRST) * ASCII_GLYPH_LEN;
        return (off + ASCII_GLYPH_LEN <= s_ascii_len) ? s_ascii + off : NULL;
    }

    if (s_cjk == NULL) {
        return NULL;
    }
    int lo = 0;
    int hi = (int)s_cjk_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t cp = rd_u16(s_cjk_codes + (size_t)mid * 2);
        if (cp == codepoint) {
            return s_cjk_bitmaps + (size_t)mid * CJK_GLYPH_LEN;
        }
        if (cp < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

/* 解码一个 UTF-8 码点，返回消耗字节数（非法字节按 1 字节跳过） */
static int utf8_decode(const char *s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t c = p[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        *cp = ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        *cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        *cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
              ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    *cp = c;
    return 1;
}

static void draw_glyph(int x, int y, const uint8_t *glyph, int w, bool invert)
{
    for (int ry = 0; ry < 16; ry++) {
        for (int rx = 0; rx < w; rx++) {
            uint8_t byte;
            if (w == 8) {
                byte = glyph[ry];
                byte = (uint8_t)(byte >> (7 - rx));
            } else {
                byte = glyph[ry * 2 + (rx >> 3)];
                byte = (uint8_t)(byte >> (7 - (rx & 7)));
            }
            bool on = (byte & 1) != 0;
            fb_set_pixel(x + rx, y + ry, invert ? !on : on);
        }
    }
}

int font_draw_text(int x, int y, const char *utf8, bool invert)
{
    if (utf8 == NULL) {
        return 0;
    }
    int cx = x;
    const char *p = utf8;
    while (*p) {
        uint32_t cp;
        p += utf8_decode(p, &cp);
        int w = font_glyph_width(cp);
        const uint8_t *glyph = font_get_glyph(cp);
        if (glyph == NULL) {
            if (!font_is_ascii(cp)) {
                ESP_LOGW(TAG, "missing glyph U+%04X", (unsigned)cp);
                fb_draw_rect(cx, y, 16, 16, !invert, true);
            }
            cx += w;
            continue;
        }
        draw_glyph(cx, y, glyph, w, invert);
        cx += w;
    }
    return cx - x;
}

int font_text_width(const char *utf8)
{
    if (utf8 == NULL) {
        return 0;
    }
    int width = 0;
    const char *p = utf8;
    while (*p) {
        uint32_t cp;
        p += utf8_decode(p, &cp);
        width += font_glyph_width(cp);
    }
    return width;
}
