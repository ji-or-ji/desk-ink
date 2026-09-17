#include "power.h"
#include "esp_log.h"

static const char *TAG = "power";

/* 单节 LiPo：3.0V(0%) ~ 4.2V(100%) */
#define BAT_EMPTY_MV 3000
#define BAT_FULL_MV  4200

esp_err_t power_init(void)
{
    ESP_LOGW(TAG, "not implemented yet (stage 2)");
    return ESP_OK;
}

esp_err_t power_read_mv(int *mv_out)
{
    (void)mv_out;
    return ESP_ERR_NOT_SUPPORTED;
}

int power_percent(int mv)
{
    if (mv <= BAT_EMPTY_MV) {
        return 0;
    }
    if (mv >= BAT_FULL_MV) {
        return 100;
    }
    return (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
}
