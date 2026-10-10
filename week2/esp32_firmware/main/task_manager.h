/*
 * task_manager.h - 第2周：远程采集任务管理模块
 *
 * 功能：
 *   - 定期轮询后端获取待处理采集命令
 *   - 收到命令后立即执行一次真实 IMU 采集
 *   - 回传数据绑定 request_id
 *   - 上报状态：received → completed
 *
 * 设计要点：
 *   - 任务轮询在独立 FreeRTOS Task 中运行
 *   - 执行采集时暂停周期上报，避免数据混乱
 *   - 每次只处理一条 pending 命令
 */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 任务轮询间隔（毫秒），默认 2 秒 */
#define TASK_POLL_INTERVAL_MS   2000

/**
 * @brief  启动远程任务管理 FreeRTOS Task
 *         内部会定期轮询后端 pending 命令并执行
 * @return ESP_OK 启动成功
 */
esp_err_t task_manager_start(void);

/**
 * @brief  查询当前是否正在执行远程采集任务
 *         主循环可据此暂停周期上报
 */
bool task_manager_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_MANAGER_H */