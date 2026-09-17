#pragma once

#include "esp_err.h"

/*
 * app — 顶层状态机
 *
 * 串起 ui / input / link / store，实现 ui-states.md 的交互模式：
 *   IDLE / MENU / CONFIG / TODO_NAV / TODO_EDIT / POMO
 * 启动后创建 app_task（prio 5），从 link 队列取消息、轮询五向开关。
 */

esp_err_t app_start(void);

/* 扫 LittleFS 的 img_*.bin，按 id 升序灌入内容池（启动时 / 收到新图后调用） */
esp_err_t app_scan_images(void);

/* RTC 唤醒回调：cfg_rotate 开启时自动轮换一项，然后重绘（rtc 就绪后接入） */
void app_on_rtc_wakeup(void);
