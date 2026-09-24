#include "eink.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "eink";

/* 引脚（gpio-map.md） */
#define PIN_RES   GPIO_NUM_2
#define PIN_DC    GPIO_NUM_3
#define PIN_CS    GPIO_NUM_4
#define PIN_BUSY  GPIO_NUM_8
#define PIN_SCK   GPIO_NUM_18
#define PIN_MOSI  GPIO_NUM_19

#define EINK_HOST       SPI2_HOST
#define EINK_CLK_HZ     (10 * 1000 * 1000)
#define BUSY_TIMEOUT_MS 10000
#define DMA_CHUNK       4096

static uint8_t       s_fb[EINK_FB_SIZE];
static spi_device_handle_t s_spi;
static uint8_t      *s_dma;
static bool          s_ready;

uint8_t *eink_framebuffer(void)
{
    return s_fb;
}

/* ---------- 底层收发 ---------- */

static void send_dc(int dc, const uint8_t *data, size_t len)
{
    gpio_set_level(PIN_DC, dc);
    while (len > 0) {
        size_t chunk = (len > DMA_CHUNK) ? DMA_CHUNK : len;
        memcpy(s_dma, data, chunk);
        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = s_dma,
        };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
        data += chunk;
        len -= chunk;
    }
}

static void cmd(uint8_t c)
{
    send_dc(0, &c, 1);
}

static void data(const uint8_t *d, size_t n)
{
    if (n > 0) {
        send_dc(1, d, n);
    }
}

static void cmd_data(uint8_t c, const uint8_t *d, size_t n)
{
    cmd(c);
    data(d, n);
}

/* 写图：面板 RAM 极性相反，逐字节取反 */
static void send_image(const uint8_t *fb, size_t len)
{
    gpio_set_level(PIN_DC, 1);
    while (len > 0) {
        size_t chunk = (len > DMA_CHUNK) ? DMA_CHUNK : len;
        for (size_t i = 0; i < chunk; i++) {
            s_dma[i] = (uint8_t)~fb[i];
        }
        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = s_dma,
        };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
        fb += chunk;
        len -= chunk;
    }
}

static esp_err_t wait_busy(const char *where)
{
    int waited = 0;
    while (gpio_get_level(PIN_BUSY)) {
        if (waited >= BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "BUSY timeout at %s", where);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    return ESP_OK;
}

/* ---------- framebuffer 绘制原语 ---------- */

void fb_clear(bool white)
{
    memset(s_fb, white ? 0x00 : 0xFF, sizeof(s_fb));
}

void fb_set_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= EINK_WIDTH || y < 0 || y >= EINK_HEIGHT) {
        return;
    }
    uint8_t *byte = &s_fb[y * EINK_FB_STRIDE + (x >> 3)];
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    if (black) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

void fb_draw_hline(int x0, int x1, int y, bool black)
{
    if (x0 > x1) {
        int t = x0; x0 = x1; x1 = t;
    }
    for (int x = x0; x <= x1; x++) {
        fb_set_pixel(x, y, black);
    }
}

void fb_draw_vline(int x, int y0, int y1, bool black)
{
    if (y0 > y1) {
        int t = y0; y0 = y1; y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        fb_set_pixel(x, y, black);
    }
}

void fb_draw_rect(int x, int y, int w, int h, bool black, bool fill)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (fill) {
        for (int yy = y; yy <= y1; yy++) {
            fb_draw_hline(x, x1, yy, black);
        }
        return;
    }
    fb_draw_hline(x, x1, y, black);
    fb_draw_hline(x, x1, y1, black);
    fb_draw_vline(x, y, y1, black);
    fb_draw_vline(x1, y, y1, black);
}

void fb_blit_1bit(const uint8_t *data, int x, int y, int w, int h, bool invert)
{
    if (data == NULL || w <= 0 || h <= 0) {
        return;
    }
    int stride = (w + 7) / 8;
    for (int ry = 0; ry < h; ry++) {
        for (int rx = 0; rx < w; rx++) {
            uint8_t bit = (data[ry * stride + (rx >> 3)] >> (7 - (rx & 7))) & 1;
            if (invert) {
                bit ^= 1;
            }
            fb_set_pixel(x + rx, y + ry, bit != 0);
        }
    }
}

/* ---------- 初始化 ---------- */

esp_err_t eink_init(void)
{
    fb_clear(true);

    const gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_RES) | (1ULL << PIN_DC),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    const gpio_config_t in_cfg = {
        .pin_bit_mask = 1ULL << PIN_BUSY,
        .mode = GPIO_MODE_INPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    s_dma = heap_caps_malloc(DMA_CHUNK, MALLOC_CAP_DMA);
    if (s_dma == NULL) {
        ESP_LOGE(TAG, "DMA buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DMA_CHUNK,
    };
    esp_err_t err = spi_bus_initialize(EINK_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }
    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = EINK_CLK_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(EINK_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 复位：RES# 低 → 高 */
    gpio_set_level(PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(wait_busy("reset"));

    /* SSD1619AZ 初始化序列（规格书 10.2） */
    cmd(0x12);                                          /* 软件复位 */
    ESP_ERROR_CHECK(wait_busy("sw reset"));

    static const uint8_t v74[] = { 0x54 };
    static const uint8_t v7e[] = { 0x3B };
    static const uint8_t v2b[] = { 0x04, 0x63 };
    static const uint8_t v0c[] = { 0x8B, 0x9C, 0x96, 0x0F };
    static const uint8_t v01[] = { 0x2B, 0x01, 0x00 };
    static const uint8_t v11[] = { 0x01 };
    static const uint8_t v44[] = { 0x00, 0x31 };
    static const uint8_t v45[] = { 0x2B, 0x01, 0x00, 0x00 };
    static const uint8_t v3c[] = { 0x01 };
    static const uint8_t v18[] = { 0x80 };
    static const uint8_t v22b1[] = { 0xB1 };

    cmd_data(0x74, v74, sizeof(v74));   /* 模拟块控制 */
    cmd_data(0x7E, v7e, sizeof(v7e));   /* 数字块控制 */
    cmd_data(0x2B, v2b, sizeof(v2b));   /* VCOM 控制 */
    cmd_data(0x0C, v0c, sizeof(v0c));   /* Booster 软启动 */
    cmd_data(0x01, v01, sizeof(v01));   /* 显示尺寸 / 驱动输出 */
    cmd_data(0x11, v11, sizeof(v11));   /* RAM 数据入口模式 */
    cmd_data(0x44, v44, sizeof(v44));   /* RAM X 地址 */
    cmd_data(0x45, v45, sizeof(v45));   /* RAM Y 地址 */
    cmd_data(0x3C, v3c, sizeof(v3c));   /* 边框 */
    cmd_data(0x18, v18, sizeof(v18));   /* 内置温度传感器 */
    cmd_data(0x22, v22b1, sizeof(v22b1)); /* 载入 LUT */
    cmd(0x20);                          /* 激活 */
    ESP_ERROR_CHECK(wait_busy("init activate"));

    s_ready = true;
    ESP_LOGI(TAG, "SSD1619AZ ready (%dx%d, %d MHz)", EINK_WIDTH, EINK_HEIGHT, EINK_CLK_HZ / 1000000);
    return ESP_OK;
}

esp_err_t eink_flush_full(void)
{
    if (!s_ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    static const uint8_t v4e[] = { 0x00 };
    static const uint8_t v4f[] = { 0x2B, 0x01 };
    static const uint8_t v22c7[] = { 0xC7 };

    cmd_data(0x4E, v4e, sizeof(v4e));   /* RAM X 计数 */
    cmd_data(0x4F, v4f, sizeof(v4f));   /* RAM Y 计数 */
    cmd(0x24);                          /* 写 BW RAM（取反） */
    send_image(s_fb, EINK_FB_SIZE);
    cmd_data(0x22, v22c7, sizeof(v22c7)); /* 图像更新 */
    cmd(0x20);                          /* 激活 */
    return wait_busy("refresh");
}

/*
 * 局部刷新（SSD1619AZ）
 *
 * 几何与数据通路按 SSD1619AZ 的通用序列实现，窗口约定与 eink_flush_full 完全一致：
 * 0x45 的 Y 是「起点 高 终点 高」四个字节，起点取面板坐标的 y1-1，终点取 y0，
 * 因为本驱动的数据入口模式（0x11=0x01）下 Y 计数是递减的（整屏那套就是从 299 写到 0）。
 *
 * 波形参数 EINK_PARTIAL_UPDATE_PARAM 是占位值。仓库里没有规格书 LUT 表，
 * 局部刷新到底用哪套波形（是否要先用 0x32 写独立 LUT、0x22 取哪个参数）
 * 必须等硬件到位后实测确认。见 会话交接-BadApple.md 第二节。
 */
#ifndef EINK_PARTIAL_UPDATE_PARAM
#define EINK_PARTIAL_UPDATE_PARAM 0xCF
#endif

esp_err_t eink_flush_partial(int x, int y, int w, int h)
{
    if (!s_ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (w <= 0 || h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 裁剪到屏幕内 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= EINK_WIDTH || y >= EINK_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (x + w > EINK_WIDTH)  { w = EINK_WIDTH - x; }
    if (y + h > EINK_HEIGHT) { h = EINK_HEIGHT - y; }

    /* 面板 RAM 的 X 地址以字节（8 像素）为单位，窗口向外取整 */
    int x0 = x & ~7;
    int x1 = (x + w + 7) & ~7;
    if (x1 > EINK_WIDTH) {
        x1 = EINK_WIDTH;
    }
    int y0 = y;
    int y1 = y + h;

    int xs = x0 / 8;
    int xe = x1 / 8 - 1;
    int ys = EINK_HEIGHT - 1 - y0;   /* 面板 Y 起点（计数从这里递减） */
    int ye = EINK_HEIGHT - 1 - (y1 - 1);

    const uint8_t v44[] = { (uint8_t)xs, (uint8_t)xe };
    const uint8_t v45[] = {
        (uint8_t)(ys & 0xFF), (uint8_t)(ys >> 8),
        (uint8_t)(ye & 0xFF), (uint8_t)(ye >> 8),
    };
    const uint8_t v4e[] = { (uint8_t)xs };
    const uint8_t v4f[] = { (uint8_t)(ys & 0xFF), (uint8_t)(ys >> 8) };

    cmd_data(0x44, v44, sizeof(v44));   /* RAM X 地址范围（字节） */
    cmd_data(0x45, v45, sizeof(v45));   /* RAM Y 地址范围（像素，起点在下） */
    cmd_data(0x4E, v4e, sizeof(v4e));   /* RAM X 计数 */
    cmd_data(0x4F, v4f, sizeof(v4f));   /* RAM Y 计数 */

    cmd(0x24);                          /* 写 BW RAM（取反） */

    /* 只发这一块：每行从 x0/8 起 (x1-x0)/8 字节，逐字节取反 */
    const int row_bytes = (x1 - x0) / 8;
    gpio_set_level(PIN_DC, 1);
    for (int row = y0; row < y1; row++) {
        const uint8_t *src = &s_fb[row * EINK_FB_STRIDE + xs];
        int remaining = row_bytes;
        while (remaining > 0) {
            int chunk = (remaining > DMA_CHUNK) ? DMA_CHUNK : remaining;
            for (int i = 0; i < chunk; i++) {
                s_dma[i] = (uint8_t)~src[i];
            }
            spi_transaction_t t = {
                .length = chunk * 8,
                .tx_buffer = s_dma,
            };
            ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
            src += chunk;
            remaining -= chunk;
        }
    }

    const uint8_t v22[] = { EINK_PARTIAL_UPDATE_PARAM };
    cmd_data(0x22, v22, sizeof(v22));   /* 图像更新（局部波形，参数待实测） */
    cmd(0x20);                          /* 激活 */
    return wait_busy("partial refresh");
}

esp_err_t eink_sleep(void)
{
    if (!s_ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    static const uint8_t v10[] = { 0x01 };
    cmd_data(0x10, v10, sizeof(v10));
    return wait_busy("panel sleep");
}
