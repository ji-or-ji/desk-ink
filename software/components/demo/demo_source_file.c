#include "demo_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

/*
 * BAF1 文件源
 *
 * 容器格式见 tools/badapple/README.md。这里只读：
 *   头部 16 字节 -> 索引 frame_count*8 字节 -> 帧记录（8 字节 dirty + PackBits 载荷）。
 * 索引按需 fseek，不整份读进内存。
 */

static const char *TAG = "demo_src";

#define BAF_HEADER_SIZE  16
#define BAF_INDEX_SIZE   8
#define BAF_FRAME_HDR    8

typedef struct {
    FILE *f;
} file_ctx_t;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* PackBits 解码，与 PC 端 packbits_encode 对应 */
static esp_err_t packbits_decode(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t i = 0;
    size_t o = 0;
    while (i < in_len && o < out_cap) {
        uint8_t control = in[i++];
        if (control < 128) {
            size_t n = (size_t)control + 1;
            if (i + n > in_len) {
                break;
            }
            size_t cp = (n < out_cap - o) ? n : (out_cap - o);
            memcpy(out + o, in + i, cp);
            i += n;
            o += cp;
        } else if (control > 128) {
            size_t n = (size_t)257 - control;
            if (i >= in_len) {
                break;
            }
            size_t cp = (n < out_cap - o) ? n : (out_cap - o);
            memset(out + o, in[i], cp);
            i++;
            o += cp;
        }
        /* control == 128：空操作 */
    }
    if (out_len) {
        *out_len = o;
    }
    return (o == out_cap) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t file_open(demo_source_t *self, const char *uri)
{
    FILE *f = fopen(uri, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "open '%s' failed", uri);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t hdr[BAF_HEADER_SIZE];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    if (memcmp(hdr, "BAF1", 4) != 0) {
        ESP_LOGE(TAG, "bad magic in '%s'", uri);
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t version = hdr[4];
    uint8_t rle_id = hdr[5];
    if (version != 1 || rle_id != 1) {
        ESP_LOGE(TAG, "unsupported version=%u rle=%u", version, rle_id);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint16_t width = rd16(hdr + 8);
    uint16_t height = rd16(hdr + 10);
    uint16_t fps_milli = rd16(hdr + 12);
    uint16_t count = rd16(hdr + 14);
    if (width == 0 || height == 0 || count == 0) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    file_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    ctx->f = f;

    self->width = width;
    self->height = height;
    self->stride = (uint16_t)((width + 7) / 8);
    self->fps = (uint16_t)((fps_milli + 500) / 1000);
    self->frame_count = count;
    self->ctx = ctx;

    ESP_LOGI(TAG, "opened %s: %ux%u %u frame(s) @ %u fps",
             uri, width, height, count, self->fps);
    return ESP_OK;
}

static esp_err_t file_read_frame(demo_source_t *self, uint32_t index,
                                 uint8_t *packed, size_t packed_cap, size_t *packed_len,
                                 uint16_t dirty[4])
{
    file_ctx_t *ctx = (file_ctx_t *)self->ctx;
    if (ctx == NULL || index >= self->frame_count) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t entry[BAF_INDEX_SIZE];
    if (fseek(ctx->f, BAF_HEADER_SIZE + (long)index * BAF_INDEX_SIZE, SEEK_SET) != 0 ||
        fread(entry, 1, sizeof(entry), ctx->f) != sizeof(entry)) {
        return ESP_FAIL;
    }
    uint32_t offset = rd32(entry);
    uint32_t length = rd32(entry + 4);
    if (length < BAF_FRAME_HDR) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t fh[BAF_FRAME_HDR];
    if (fseek(ctx->f, (long)offset, SEEK_SET) != 0 ||
        fread(fh, 1, sizeof(fh), ctx->f) != sizeof(fh)) {
        return ESP_FAIL;
    }
    dirty[0] = rd16(fh);
    dirty[1] = rd16(fh + 2);
    dirty[2] = rd16(fh + 4);
    dirty[3] = rd16(fh + 6);

    size_t payload_len = length - BAF_FRAME_HDR;
    uint8_t *payload = malloc(payload_len);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (fread(payload, 1, payload_len, ctx->f) != payload_len) {
        free(payload);
        return ESP_FAIL;
    }

    size_t raw = (size_t)self->stride * self->height;
    esp_err_t err = packbits_decode(payload, payload_len, packed, packed_cap, &raw);
    free(payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "frame %u decode short (%u/%u)",
                 (unsigned)index, (unsigned)raw, (unsigned)(self->stride * self->height));
        return err;
    }
    if (packed_len) {
        *packed_len = raw;
    }
    return ESP_OK;
}

static void file_close(demo_source_t *self)
{
    file_ctx_t *ctx = (file_ctx_t *)self->ctx;
    if (ctx) {
        if (ctx->f) {
            fclose(ctx->f);
        }
        free(ctx);
        self->ctx = NULL;
    }
}

static demo_source_t s_file_source = {
    .name = "file",
    .open = file_open,
    .read_frame = file_read_frame,
    .close = file_close,
};

demo_source_t *demo_source_file(void)
{
    return &s_file_source;
}
