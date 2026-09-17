#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * store — 配置与文件
 *
 * NVS：小配置项（配置页三项、番茄钟起始时刻、轮换计数、待办勾选状态）
 * LittleFS：大块数据（课表缓存 / 待办 / 插画 / 一言库），已接入 joltwallet/littlefs
 */

esp_err_t store_init(void);

/* NVS */
esp_err_t store_get_u32(const char *key, uint32_t *out);
esp_err_t store_set_u32(const char *key, uint32_t val);
esp_err_t store_get_str(const char *key, char *buf, size_t len);
esp_err_t store_set_str(const char *key, const char *val);

/* 文件（LittleFS，挂载在 /littlefs） */
esp_err_t store_write_file(const char *path, const void *data, size_t len);
esp_err_t store_read_file(const char *path, void *buf, size_t cap, size_t *out_len);
esp_err_t store_delete_file(const char *path);
