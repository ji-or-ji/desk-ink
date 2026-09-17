#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * link_json — 极简 JSON 取值工具（内部使用）
 *
 * ESP-IDF v6.0.1 已移除内置 cJSON，消息结构固定，故自写轻量解析。
 * 只支持取值，不构建 DOM；容忍空指针（未找到时返回 false）。
 */

/* obj 指向 '{'，查找顶层字段，返回值的起始指针；未找到返回 NULL */
const char *json_obj_get(const char *obj, const char *key);

/* value 指向字符串字面量，解码（含 \uXXXX / 代理对）到 out */
bool json_value_str(const char *value, char *out, size_t out_size);

bool json_value_int(const char *value, int *out);
bool json_value_bool(const char *value, bool *out);

/* arr 指向 '['；返回第一个元素值指针，空数组返回 NULL */
const char *json_array_first(const char *arr);
/* cur 为当前元素值指针，返回下一个元素值指针；末尾返回 NULL */
const char *json_array_next(const char *cur);
