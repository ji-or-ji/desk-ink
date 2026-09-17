#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/* power — 电池电压（MCP3021A3T，I2C 0x4B，10-bit）
 * 总线由 main 创建一次，handle 传入。 */
esp_err_t power_init(i2c_master_bus_handle_t bus);

esp_err_t power_read_mv(int *mv_out);
int       power_percent(int mv);
