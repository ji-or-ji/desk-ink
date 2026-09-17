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
