#include "input.h"
#include "esp_log.h"

static const char *TAG = "input";

esp_err_t input_init(void)
{
    ESP_LOGW(TAG, "not implemented yet (stage 2)");
    return ESP_OK;
}

bool input_poll(input_event_t *out)
{
    (void)out;
    return false;
}

bool input_woke_from_switch(void)
{
    return false;
}
