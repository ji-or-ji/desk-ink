#pragma once

#include <stdbool.h>
#include "esp_err.h"

/*
 * input — 五向导航开关
 *
 * GPIO0 一个脚读出五个方向：各方向串不同阻值到 COM，ADC1_CH0 读分压值查表。
 * 深睡前配数字输入 + GPIO 低电平唤醒（C3 无 RTCIO/EXT1）；唤醒后切 ADC 读方向。
 * 电压判读表见 encoder-arch.md。
 */

typedef enum {
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_CENTER_SHORT,
    INPUT_CENTER_LONG,
} input_event_t;

esp_err_t input_init(void);

/* 非阻塞取事件，无事件返回 false */
bool input_poll(input_event_t *out);

/* 深睡后被 GPIO0 EXT1 唤醒时返回 true */
bool input_woke_from_switch(void);

/* 深睡前调用：GPIO0 切数字输入 + 上拉，并使能 EXT1 低电平唤醒 */
void input_arm_wakeup(void);
