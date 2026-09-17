#include "rtc.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "rtc";

#define DS3231_ADDR  0x68
#define REG_TIME     0x00   /* 秒 分 时 星期 日 月 年 */
#define REG_ALARM1   0x07   /* A1 秒 分 时 日 */
#define REG_CONTROL  0x0E
#define REG_STATUS   0x0F

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2dec(uint8_t b)
{
    return (uint8_t)((b >> 4) * 10 + (b & 0x0F));
}

static uint8_t dec2bcd(uint8_t d)
{
    return (uint8_t)(((d / 10) << 4) | (d % 10));
}

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

static esp_err_t write_regs(uint8_t reg, const uint8_t *buf, size_t n)
{
    uint8_t tmp[16];
    if (n + 1 > sizeof(tmp)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tmp[0] = reg;
    memcpy(tmp + 1, buf, n);
    return i2c_master_transmit(s_dev, tmp, n + 1, 100);
}

esp_err_t ds3231_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        return err;
    }
    if (i2c_master_probe(bus, DS3231_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 not responding at 0x%02X", DS3231_ADDR);
    }
    ESP_LOGI(TAG, "DS3231 ready");
    return ESP_OK;
}

esp_err_t ds3231_get_time(struct tm *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t b[7];
    esp_err_t err = read_regs(REG_TIME, b, sizeof(b));
    if (err != ESP_OK) {
        return err;
    }
    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd2dec(b[0] & 0x7F);
    out->tm_min = bcd2dec(b[1] & 0x7F);

    uint8_t hr = b[2];
    if (hr & 0x40) {                       /* 12 小时制 */
        int h = bcd2dec(hr & 0x1F);
        if (hr & 0x20) {
            h += 12;
        }
        out->tm_hour = h % 24;
    } else {
        out->tm_hour = bcd2dec(hr & 0x3F);
    }

    out->tm_wday = (bcd2dec(b[3] & 0x07) + 6) % 7;   /* DS3231 1=周日 → tm 0=周日 */
    out->tm_mday = bcd2dec(b[4] & 0x3F);
    out->tm_mon  = bcd2dec(b[5] & 0x1F) - 1;
    out->tm_year = 100 + bcd2dec(b[6]);
    return ESP_OK;
}

esp_err_t ds3231_set_time(const struct tm *t)
{
    if (t == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t b[7];
    b[0] = dec2bcd((uint8_t)t->tm_sec);
    b[1] = dec2bcd((uint8_t)t->tm_min);
    b[2] = dec2bcd((uint8_t)t->tm_hour);        /* 24 小时制，bit6=0 */
    b[3] = dec2bcd((uint8_t)(t->tm_wday + 1));  /* tm 0=周日 → DS3231 1=周日 */
    b[4] = dec2bcd((uint8_t)t->tm_mday);
    b[5] = dec2bcd((uint8_t)(t->tm_mon + 1));
    b[6] = dec2bcd((uint8_t)(t->tm_year % 100));
    return write_regs(REG_TIME, b, sizeof(b));
}

esp_err_t ds3231_set_alarm(const struct tm *t)
{
    if (t == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t a[4];
    a[0] = dec2bcd((uint8_t)t->tm_sec);
    a[1] = dec2bcd((uint8_t)t->tm_min);
    a[2] = dec2bcd((uint8_t)t->tm_hour);
    a[3] = 0x80;                                /* A1M4=1：日不参与匹配（每天） */
    esp_err_t err = write_regs(REG_ALARM1, a, sizeof(a));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t ctrl;
    err = read_regs(REG_CONTROL, &ctrl, 1);
    if (err != ESP_OK) {
        return err;
    }
    ctrl |= (1 << 2) | (1 << 0);                /* INTCN=1, A1IE=1 */
    err = write_regs(REG_CONTROL, &ctrl, 1);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t status;
    err = read_regs(REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    status &= (uint8_t)~0x01;                   /* 清 A1F */
    return write_regs(REG_STATUS, &status, 1);
}

esp_err_t ds3231_clear_alarm(void)
{
    uint8_t ctrl;
    esp_err_t err = read_regs(REG_CONTROL, &ctrl, 1);
    if (err != ESP_OK) {
        return err;
    }
    ctrl &= (uint8_t)~(1 << 0);                 /* A1IE=0 */
    err = write_regs(REG_CONTROL, &ctrl, 1);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t status;
    err = read_regs(REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    status &= (uint8_t)~0x01;
    return write_regs(REG_STATUS, &status, 1);
}
