#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * link — 蓝牙串口（UART0）与协议
 *
 * 帧格式 JSON Lines，单行 ≤255 字节，详见 protocol.md。
 * 独占 UART0（GPIO20 RX / GPIO21 TX，115200 8N1），运行在 link_task；
 * 日志走 USB Serial/JTAG。
 */

#define LINK_LINE_MAX  256
#define LINK_IMG_MAX   16384   /* 400×300 单色约 15KB */

typedef struct {
    int  state;              /* 0=无 1=上课 2=准备上课 3=课间 4=放学 */
    int  cur_period;
    char cur_name[32];
    char cur_initial[8];
    char cur_start[6];
    char cur_end[6];
    int  cur_left;
    char next_name[32];
    char next_start[6];
    char next_end[6];
} schedule_t;

typedef struct {
    int  id;
    bool done;
    char text[48];
} todo_item_t;

#define LINK_TODO_MAX 8

typedef struct {
    int          count;
    todo_item_t  items[LINK_TODO_MAX];
} todo_list_t;

/* 设备端配置全量（字段名 = NVS key 去掉 cfg_ 前缀） */
typedef struct {
    int refresh;      /* 0 自动 / 1 常规 / 2 静默 */
    int pomo_focus;   /* 15 / 25 / 45 */
    int pomo_break;   /* 5 / 10 */
    int rotate;       /* 0 关 / 1 开 */
    int art_mode;     /* 0 全屏 / 1 小窗 */
    int bar_mode;     /* 0 实心 / 1 反色 / 2 反色自适应 */
} link_cfg_t;

typedef enum {
    MSG_SCHEDULE,
    MSG_NEWS,
    MSG_QUOTE,
    MSG_TODO,
    MSG_IMAGE,
    MSG_UNKNOWN,
} msg_type_t;

typedef struct {
    msg_type_t type;
    char       raw[LINK_LINE_MAX];   /* 原始 JSON 行（img_chunk 除外） */
    union {
        schedule_t  schedule;
        todo_list_t todo;
        struct { char text[192]; } quote;
        struct { char title[64]; char body[160]; } news;
        /* data 指向 link 内部缓冲，仅在回调期间有效 */
        struct { int id; char name[24]; size_t len; const uint8_t *data; } image;
    } u;
} parsed_msg_t;

typedef void (*link_rx_cb_t)(const parsed_msg_t *msg);

esp_err_t link_init(void);

esp_err_t link_send_schedule(const schedule_t *s);
esp_err_t link_send_todo(const todo_list_t *list);
esp_err_t link_send_cfg(const link_cfg_t *cfg);
esp_err_t link_send_req(const char *what);
esp_err_t link_send_hello(const char *fw_ver);
esp_err_t link_send_status(int bat_mv);
esp_err_t link_send_ack(const char *what);

void link_set_rx_callback(link_rx_cb_t cb);
