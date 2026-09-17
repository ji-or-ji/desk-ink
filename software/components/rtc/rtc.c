#include "rtc.h"
#include "esp_log.h"

static const char *TAG = "rtc";

esp_err_t rtc_init(void)
{
    ESP_LOGW(TAG, "not implemented yet (stage 2)");
    return ESP_OK;
}

esp_err_t rtc_get_time(struct tm *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t rtc_set_time(const struct tm *t)
{
    (void)t;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t rtc_set_alarm(const struct tm *t)
{
    (void)t;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t rtc_clear_alarm(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
