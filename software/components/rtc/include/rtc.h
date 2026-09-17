#pragma once

#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* rtc — DS3231（I2C 0x68，SQW → GPIO7 唤醒）
 * 总线由 main 创建一次，handle 传入，避免各模块各建总线。
 * 注：函数名用 ds3231_ 前缀，避免与 ESP-IDF 内部 rtc_init 符号冲突。 */
esp_err_t ds3231_init(i2c_master_bus_handle_t bus);

esp_err_t ds3231_get_time(struct tm *out);
esp_err_t ds3231_set_time(const struct tm *t);

/* 闹钟 1：每天在 t 的时/分/秒触发（星期不参与匹配），SQW 低有效 */
esp_err_t ds3231_set_alarm(const struct tm *t);
esp_err_t ds3231_clear_alarm(void);
