#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * demo_source — 播放器的帧来源接口
 *
 * 只抽一层间接，方便以后接 USB 串口实时流；当前只实现文件源（BAF1 + LittleFS）。
 * 位图约定与 eink / tools/badapple 一致：行优先、每行 ceil(width/8) 字节、
 * MSB 在左、1 = 黑。
 */

typedef struct demo_source demo_source_t;

struct demo_source {
    const char *name;

    /* open 之后由源填好，播放器读这些字段 */
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t fps;
    uint32_t frame_count;

    void *ctx;

    esp_err_t (*open)(demo_source_t *self, const char *uri);
    /* 解出第 index 帧的完整位图（stride*height 字节）与 dirty 区域 */
    esp_err_t (*read_frame)(demo_source_t *self, uint32_t index,
                            uint8_t *packed, size_t packed_cap, size_t *packed_len,
                            uint16_t dirty[4]);
    void (*close)(demo_source_t *self);
};

/* BAF1 文件源单例（LittleFS 上的绝对路径） */
demo_source_t *demo_source_file(void);
