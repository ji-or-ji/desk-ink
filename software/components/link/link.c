#include "link.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "esp_log.h"

#include "link_json.h"

static const char *TAG = "link";

#define LINK_UART     UART_NUM_0
#define LINK_TX_GPIO  21
#define LINK_RX_GPIO  20
#define LINK_BAUD     115200

static link_rx_cb_t      s_rx_cb;
static SemaphoreHandle_t s_tx_mutex;
static parsed_msg_t      s_msg;          /* link_task 独占 */

/* ---------- 插画分片组包 ---------- */
static uint8_t s_img_buf[LINK_IMG_MAX];
static int     s_img_id = -1;
static size_t  s_img_len;
static size_t  s_img_expected;
static int     s_img_seq_next;

/* ---------- 待办分片组包 ---------- */
static todo_list_t s_todo_acc;
static int         s_todo_seq_next;
static int         s_todo_total;
static bool        s_todo_active;

/* ---------- 工具 ---------- */

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t cap)
{
    size_t o = 0;
    int quad[4];
    int qi = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '=') {
            break;
        }
        if (c == '\n' || c == '\r') {
            continue;
        }
        int v = b64_val(c);
        if (v < 0) {
            continue;
        }
        quad[qi++] = v;
        if (qi == 4) {
            uint32_t n = ((uint32_t)quad[0] << 18) | ((uint32_t)quad[1] << 12) |
                         ((uint32_t)quad[2] << 6) | (uint32_t)quad[3];
            if (o < cap) out[o++] = (uint8_t)(n >> 16);
            if (o < cap) out[o++] = (uint8_t)(n >> 8);
            if (o < cap) out[o++] = (uint8_t)n;
            qi = 0;
        }
    }
    if (qi == 3) {
        uint32_t n = ((uint32_t)quad[0] << 18) | ((uint32_t)quad[1] << 12) | ((uint32_t)quad[2] << 6);
        if (o < cap) out[o++] = (uint8_t)(n >> 16);
        if (o < cap) out[o++] = (uint8_t)(n >> 8);
    } else if (qi == 2) {
        uint32_t n = ((uint32_t)quad[0] << 18) | ((uint32_t)quad[1] << 12);
        if (o < cap) out[o++] = (uint8_t)(n >> 16);
    }
    return o;
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void json_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (in == NULL) {
        in = "";
    }
    for (; *in && o + 1 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        const char *rep = NULL;
        switch (c) {
        case '"':  rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n";  break;
        case '\r': rep = "\\r";  break;
        case '\t': rep = "\\t";  break;
        default: break;
        }
        if (rep) {
            if (o + 2 >= cap) break;
            out[o++] = rep[0];
            out[o++] = rep[1];
        } else if (c >= 0x20) {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static esp_err_t uart_write_line(const char *s)
{
    if (s == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = strlen(s);
    if (len > 255) {
        ESP_LOGW(TAG, "frame exceeds 255 bytes (%u)", (unsigned)len);
    }
    if (s_tx_mutex) {
        xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    }
    int w = uart_write_bytes(LINK_UART, s, len);
    uart_write_bytes(LINK_UART, "\n", 1);
    if (s_tx_mutex) {
        xSemaphoreGive(s_tx_mutex);
    }
    return (w == (int)len) ? ESP_OK : ESP_FAIL;
}

/* ---------- 接收解析 ---------- */

static void parse_schedule(const char *line, schedule_t *s)
{
    json_value_int(json_obj_get(line, "state"), &s->state);
    const char *cur = json_obj_get(line, "cur");
    if (cur) {
        json_value_str(json_obj_get(cur, "name"), s->cur_name, sizeof(s->cur_name));
        json_value_str(json_obj_get(cur, "initial"), s->cur_initial, sizeof(s->cur_initial));
        json_value_str(json_obj_get(cur, "start"), s->cur_start, sizeof(s->cur_start));
        json_value_str(json_obj_get(cur, "end"), s->cur_end, sizeof(s->cur_end));
        json_value_int(json_obj_get(cur, "left"), &s->cur_left);
    }
    const char *next = json_obj_get(line, "next");
    if (next) {
        json_value_str(json_obj_get(next, "name"), s->next_name, sizeof(s->next_name));
        json_value_str(json_obj_get(next, "start"), s->next_start, sizeof(s->next_start));
        json_value_str(json_obj_get(next, "end"), s->next_end, sizeof(s->next_end));
    }
}

/* 待办分片：seq==0 开始新任务，逐片累加，收齐 total 片后整体回调 */
static void handle_todo(const char *line)
{
    int seq = 0;
    int total = 1;
    if (!json_value_int(json_obj_get(line, "seq"), &seq)) {
        seq = 0;
    }
    if (!json_value_int(json_obj_get(line, "total"), &total) || total <= 0) {
        total = 1;
    }

    if (seq == 0) {
        s_todo_acc.count = 0;
        s_todo_seq_next = 0;
        s_todo_total = total;
        s_todo_active = true;
    }
    if (!s_todo_active || seq != s_todo_seq_next) {
        ESP_LOGW(TAG, "todo seq jump (got %d want %d), drop task", seq, s_todo_seq_next);
        s_todo_active = false;
        return;
    }

    const char *it = json_array_first(json_obj_get(line, "items"));
    while (it && s_todo_acc.count < LINK_TODO_MAX) {
        todo_item_t *item = &s_todo_acc.items[s_todo_acc.count];
        json_value_int(json_obj_get(it, "id"), &item->id);
        json_value_bool(json_obj_get(it, "done"), &item->done);
        json_value_str(json_obj_get(it, "text"), item->text, sizeof(item->text));
        s_todo_acc.count++;
        it = json_array_next(it);
    }
    s_todo_seq_next++;

    if (s_todo_seq_next >= s_todo_total) {
        s_todo_active = false;
        ESP_LOGI(TAG, "todo assembled: %d item(s) from %d frame(s)", s_todo_acc.count, s_todo_total);
        if (s_rx_cb) {
            memset(&s_msg, 0, sizeof(s_msg));
            s_msg.type = MSG_TODO;
            s_msg.u.todo = s_todo_acc;
            size_t len = strlen(line);
            size_t cp = len < sizeof(s_msg.raw) - 1 ? len : sizeof(s_msg.raw) - 1;
            memcpy(s_msg.raw, line, cp);
            s_rx_cb(&s_msg);
        }
    }
}

static void handle_img(const char *type, const char *line)
{
    if (strcmp(type, "img_begin") == 0) {
        int id = -1;
        int bytes = 0;
        json_value_int(json_obj_get(line, "id"), &id);
        json_value_int(json_obj_get(line, "bytes"), &bytes);
        if (bytes < 0 || (size_t)bytes > sizeof(s_img_buf)) {
            ESP_LOGW(TAG, "img_begin bad size %d", bytes);
            s_img_id = -1;
            return;
        }
        s_img_id = id;
        s_img_len = 0;
        s_img_expected = (size_t)bytes;
        s_img_seq_next = 0;
        ESP_LOGI(TAG, "img_begin id=%d bytes=%d", id, bytes);
    } else if (strcmp(type, "img_chunk") == 0) {
        int id = -1;
        int seq = -1;
        json_value_int(json_obj_get(line, "id"), &id);
        json_value_int(json_obj_get(line, "seq"), &seq);
        if (id != s_img_id || seq != s_img_seq_next) {
            ESP_LOGW(TAG, "img_chunk out of sequence, drop task");
            s_img_id = -1;
            return;
        }
        char data[256] = {0};
        json_value_str(json_obj_get(line, "data"), data, sizeof(data));
        size_t n = base64_decode(data, strlen(data), s_img_buf + s_img_len,
                                 sizeof(s_img_buf) - s_img_len);
        s_img_len += n;
        s_img_seq_next++;
    } else if (strcmp(type, "img_end") == 0) {
        int id = -1;
        int crc = 0;
        json_value_int(json_obj_get(line, "id"), &id);
        json_value_int(json_obj_get(line, "crc"), &crc);
        if (id != s_img_id) {
            s_img_id = -1;
            return;
        }
        uint16_t got = crc16_ccitt(s_img_buf, s_img_len);
        s_img_id = -1;
        if ((uint16_t)crc != got) {
            ESP_LOGW(TAG, "img_end crc mismatch (got 0x%04X want 0x%04X)", got, (uint16_t)crc);
            return;
        }
        ESP_LOGI(TAG, "img_end id=%d len=%u", id, (unsigned)s_img_len);
        if (s_rx_cb) {
            memset(&s_msg, 0, sizeof(s_msg));
            s_msg.type = MSG_IMAGE;
            s_msg.u.image.id = id;
            s_msg.u.image.len = s_img_len;
            s_msg.u.image.data = s_img_buf;
            s_rx_cb(&s_msg);
        }
    }
}

static void dispatch_line(const char *line, size_t len)
{
    char type[16] = {0};
    const char *tv = json_obj_get(line, "t");
    if (tv == NULL) {
        tv = json_obj_get(line, "type");
    }
    if (!json_value_str(tv, type, sizeof(type))) {
        ESP_LOGW(TAG, "drop line: no type field");
        return;
    }

    if (strncmp(type, "img_", 4) == 0) {
        handle_img(type, line);
        return;
    }
    if (strcmp(type, "todo") == 0) {
        handle_todo(line);   /* 分片累加，收齐才回调 */
        return;
    }

    memset(&s_msg, 0, sizeof(s_msg));
    size_t cp = len < sizeof(s_msg.raw) - 1 ? len : sizeof(s_msg.raw) - 1;
    memcpy(s_msg.raw, line, cp);

    if (strcmp(type, "schedule") == 0) {
        s_msg.type = MSG_SCHEDULE;
        parse_schedule(line, &s_msg.u.schedule);
    } else if (strcmp(type, "quote") == 0) {
        s_msg.type = MSG_QUOTE;
        json_value_str(json_obj_get(line, "text"), s_msg.u.quote.text, sizeof(s_msg.u.quote.text));
    } else if (strcmp(type, "news") == 0) {
        s_msg.type = MSG_NEWS;
        json_value_str(json_obj_get(line, "title"), s_msg.u.news.title, sizeof(s_msg.u.news.title));
        json_value_str(json_obj_get(line, "body"), s_msg.u.news.body, sizeof(s_msg.u.news.body));
    } else {
        s_msg.type = MSG_UNKNOWN;
        ESP_LOGW(TAG, "unknown message type '%s'", type);
    }

    if (s_rx_cb) {
        s_rx_cb(&s_msg);
    }
}

static void link_task(void *arg)
{
    (void)arg;
    uint8_t buf[128];
    char line[LINK_LINE_MAX];
    size_t len = 0;

    for (;;) {
        int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), portMAX_DELAY);
        if (n <= 0) {
            continue;
        }
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                if (len > 0) {
                    line[len] = '\0';
                    dispatch_line(line, len);
                }
                len = 0;
            } else if (c != '\r') {
                if (len < LINK_LINE_MAX - 1) {
                    line[len++] = c;
                } else {
                    ESP_LOGW(TAG, "line overflow, drop");
                    len = 0;
                }
            }
        }
    }
}

esp_err_t link_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(LINK_UART, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TX_GPIO, LINK_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(link_task, "link_task", 4096, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create link_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART0 ready: TX=%d RX=%d %d 8N1", LINK_TX_GPIO, LINK_RX_GPIO, LINK_BAUD);
    return ESP_OK;
}

void link_set_rx_callback(link_rx_cb_t cb)
{
    s_rx_cb = cb;
}

/* ---------- 发送 ---------- */

esp_err_t link_send_hello(const char *fw_ver)
{
    char v[32];
    char buf[LINK_LINE_MAX];
    json_escape(fw_ver, v, sizeof(v));
    snprintf(buf, sizeof(buf), "{\"t\":\"hello\",\"fw\":\"%s\"}", v);
    return uart_write_line(buf);
}

esp_err_t link_send_req(const char *what)
{
    char w[32];
    char buf[LINK_LINE_MAX];
    json_escape(what, w, sizeof(w));
    snprintf(buf, sizeof(buf), "{\"t\":\"req\",\"what\":\"%s\"}", w);
    return uart_write_line(buf);
}

esp_err_t link_send_ack(const char *what)
{
    char w[32];
    char buf[LINK_LINE_MAX];
    json_escape(what, w, sizeof(w));
    snprintf(buf, sizeof(buf), "{\"t\":\"ack\",\"for\":\"%s\"}", w);
    return uart_write_line(buf);
}

esp_err_t link_send_status(int bat_mv)
{
    char buf[LINK_LINE_MAX];
    if (bat_mv < 0) {
        bat_mv = 0;
    }
    snprintf(buf, sizeof(buf), "{\"t\":\"status\",\"bat\":%d.%02d}",
             bat_mv / 1000, (bat_mv % 1000) / 10);
    return uart_write_line(buf);
}

esp_err_t link_send_schedule(const schedule_t *s)
{
    if (s == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char n1[80], i1[24], n2[80];
    char buf[384];
    json_escape(s->cur_name, n1, sizeof(n1));
    json_escape(s->cur_initial, i1, sizeof(i1));
    json_escape(s->next_name, n2, sizeof(n2));

    snprintf(buf, sizeof(buf),
             "{\"t\":\"schedule\",\"state\":%d,"
             "\"cur\":{\"name\":\"%s\",\"initial\":\"%s\",\"start\":\"%s\",\"end\":\"%s\",\"left\":%d},"
             "\"next\":{\"name\":\"%s\",\"start\":\"%s\",\"end\":\"%s\"}}",
             s->state, n1, i1, s->cur_start, s->cur_end, s->cur_left,
             n2, s->next_start, s->next_end);
    return uart_write_line(buf);
}

esp_err_t link_send_cfg(const link_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char buf[LINK_LINE_MAX];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"cfg\",\"refresh\":%d,\"pomo_focus\":%d,\"pomo_break\":%d,"
             "\"rotate\":%d,\"art_mode\":%d,\"bar_mode\":%d}",
             cfg->refresh, cfg->pomo_focus, cfg->pomo_break,
             cfg->rotate, cfg->art_mode, cfg->bar_mode);
    return uart_write_line(buf);
}

esp_err_t link_send_todo(const todo_list_t *list)
{
    if (list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const int per = 2;   /* 每片最多 2 条（2×72 + 外壳 ≈ 186 < 255） */
    int count = list->count;
    if (count < 0) {
        count = 0;
    }
    if (count > LINK_TODO_MAX) {
        count = LINK_TODO_MAX;
    }
    int total = (count + per - 1) / per;
    if (total == 0) {
        total = 1;   /* 空列表也发一片，表示“全量为空” */
    }

    for (int seq = 0; seq < total; seq++) {
        char buf[LINK_LINE_MAX];
        int n = snprintf(buf, sizeof(buf), "{\"t\":\"todo\",\"seq\":%d,\"total\":%d,\"items\":[",
                         seq, total);
        int start = seq * per;
        int end = start + per;
        if (end > count) {
            end = count;
        }
        for (int i = start; i < end; i++) {
            char txt[128];
            char item[200];
            json_escape(list->items[i].text, txt, sizeof(txt));
            int il = snprintf(item, sizeof(item), "%s{\"id\":%d,\"done\":%s,\"text\":\"%s\"}",
                              (i > start) ? "," : "", list->items[i].id,
                              list->items[i].done ? "true" : "false", txt);
            if (il < 0 || il >= (int)sizeof(item) || n + il + 4 > (int)sizeof(buf)) {
                ESP_LOGW(TAG, "todo item too long, skipped");
                break;
            }
            memcpy(buf + n, item, (size_t)il);
            n += il;
        }
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "]}");

        esp_err_t err = uart_write_line(buf);
        if (err != ESP_OK) {
            return err;
        }
        if (seq + 1 < total) {
            vTaskDelay(pdMS_TO_TICKS(1));   /* 片间小间隔，别灌爆蓝牙缓冲 */
        }
    }
    return ESP_OK;
}
