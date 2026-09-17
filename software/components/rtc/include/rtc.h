#pragma once

#include <time.h>
#include "esp_err.h"

/* rtc — DS3231（I2C 0x68，SQW → GPIO7 / EXT0 唤醒） */
esp_err_t rtc_init(void);

esp_err_t rtc_get_time(struct tm *out);
esp_err_t rtc_set_time(const struct tm *t);

esp_err_t rtc_set_alarm(const struct tm *t);
esp_err_t rtc_clear_alarm(void);
