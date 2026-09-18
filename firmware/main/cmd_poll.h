#ifndef CMD_POLL_H
#define CMD_POLL_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief 启动命令轮询任务
 *
 * 创建一个 FreeRTOS 任务，每隔约 3 秒查询服务器获取待处理命令。
 * 需要在 WiFi 连接成功后调用。
 */
void cmd_poll_start(void);

/**
 * @brief 查询周期上报是否已暂停
 * @return true = 已暂停，不应上报数据
 */
bool cmd_is_reporting_paused(void);

#endif