#include "input.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_sleep.h"
#include "esp_log.h"

static const char *TAG = "input";

#define INPUT_GPIO        GPIO_NUM_0
#define INPUT_ADC_UNIT    ADC_UNIT_1
#define INPUT_ADC_CHAN    ADC_CHANNEL_0   /* GPIO0 = ADC1_CH0 */
#define INPUT_ADC_ATTEN   ADC_ATTEN_DB_12
#define INPUT_ADC_WIDTH   ADC_BITWIDTH_12

#define INPUT_SAMPLE_MS   20
#define INPUT_AVG_N       8
#define INPUT_DEBOUNCE    3
#define INPUT_LONG_MS     800

/*
 * 分压判读阈值（12-bit 原始计数）。
 * 按 encoder-arch.md 建议值：R0=100k，1k/2.7k/5.6k/12k/22k，
 * 理论计数约 40 / 109 / 223 / 426 / 738，不拨 ≈ 4095。
 * ⚠️ C3 ADC 有非线性，需实测后微调这些 #define。
 */
#define TH_UP_MAX       74
#define TH_DOWN_MAX     166
#define TH_LEFT_MAX     324
#define TH_RIGHT_MAX    582
#define TH_CENTER_MAX   2000

typedef enum {
    ZONE_NONE = 0,
    ZONE_UP,
    ZONE_DOWN,
    ZONE_LEFT,
    ZONE_RIGHT,
    ZONE_CENTER,
} zone_t;

static adc_oneshot_unit_handle_t s_adc;
static QueueHandle_t             s_queue;

static zone_t classify(int raw)
{
    if (raw <= TH_UP_MAX)    return ZONE_UP;
    if (raw <= TH_DOWN_MAX)  return ZONE_DOWN;
    if (raw <= TH_LEFT_MAX)  return ZONE_LEFT;
    if (raw <= TH_RIGHT_MAX) return ZONE_RIGHT;
    if (raw <= TH_CENTER_MAX) return ZONE_CENTER;
    return ZONE_NONE;
}

static int read_avg(void)
{
    int sum = 0;
    int n = 0;
    for (int i = 0; i < INPUT_AVG_N; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, INPUT_ADC_CHAN, &raw) == ESP_OK) {
            sum += raw;
            n++;
        }
    }
    return (n > 0) ? sum / n : 4095;
}

static void push(input_event_t ev)
{
    /* 满了就丢最旧，避免阻塞采样 */
    xQueueSend(s_queue, &ev, 0);
}

static void input_task(void *arg)
{
    (void)arg;
    zone_t stable = ZONE_NONE;
    zone_t cand = ZONE_NONE;
    int stable_cnt = 0;

    bool center_held = false;
    int center_ms = 0;
    bool long_fired = false;

    for (;;) {
        zone_t z = classify(read_avg());

        if (z == cand) {
            if (stable_cnt < INPUT_DEBOUNCE) {
                stable_cnt++;
            }
        } else {
            cand = z;
            stable_cnt = 1;
        }

        if (stable_cnt >= INPUT_DEBOUNCE && z != stable) {
            if (stable != ZONE_CENTER && z == ZONE_CENTER) {
                center_held = true;
                center_ms = 0;
                long_fired = false;
            } else if (stable == ZONE_CENTER && z != ZONE_CENTER) {
                if (center_held && !long_fired) {
                    push(INPUT_CENTER_SHORT);
                }
                center_held = false;
            } else if (z >= ZONE_UP && z <= ZONE_RIGHT) {
                static const input_event_t map[] = {
                    INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT
                };
                push(map[z - ZONE_UP]);
            }
            stable = z;
        }

        if (center_held && !long_fired) {
            center_ms += INPUT_SAMPLE_MS;
            if (center_ms >= INPUT_LONG_MS) {
                push(INPUT_CENTER_LONG);
                long_fired = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(INPUT_SAMPLE_MS));
    }
}

esp_err_t input_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = INPUT_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = INPUT_ADC_ATTEN,
        .bitwidth = INPUT_ADC_WIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, INPUT_ADC_CHAN, &chan_cfg));

    s_queue = xQueueCreate(8, sizeof(input_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(input_task, "input_task", 3072, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "five-way switch ready (ADC1_CH0 / GPIO%d)", INPUT_GPIO);
    return ESP_OK;
}

bool input_poll(input_event_t *out)
{
    if (s_queue == NULL || out == NULL) {
        return false;
    }
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}

void input_arm_wakeup(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << INPUT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    /* ESP32-C3 无 RTCIO/EXT1，走 GPIO 低电平唤醒 */
    gpio_wakeup_enable(INPUT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

bool input_woke_from_switch(void)
{
    return (esp_sleep_get_wakeup_causes() & (1U << ESP_SLEEP_WAKEUP_GPIO)) != 0;
}
