#pragma once

#include "esp_err.h"

/*
 * demo — Bad Apple 播放 demo
 *
 * 由 app 在 CONFIG_APP_DEMO_BADAPP 打开时进入，跑完一轮/若干轮后返回。
 * 帧来源见 demo_source.h。
 */

esp_err_t demo_badapple_start(void);
