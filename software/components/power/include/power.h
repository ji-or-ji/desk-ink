#pragma once

#include "esp_err.h"

/* power — 电池电压（MCP3021A3T，I2C 0x4B，10-bit） */
esp_err_t power_init(void);

esp_err_t power_read_mv(int *mv_out);
int       power_percent(int mv);
