/*
 * http_upload.h - HTTP 数据上传 + 指令拉取模块
 *
 * 包含 3 个独立的 HTTP 客户端实例（各自服务于一个任务，无需互斥锁）：
 *   s_client        → upload_task 专用（POST /api/imu）
 *   s_poll_client   → cmd_poll_task 专用（GET /api/tasks/pending）
 *   s_report_client → cmd_poll_task 专用（POST /api/tasks/{id}/complete 或 /fail）
 */

#ifndef HTTP_UPLOAD_H
#define HTTP_UPLOAD_H

#include <stdbool.h>
#include "esp_err.h"
#include "qma7981.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化全部 HTTP 客户端（3 个实例）
 *         在 WiFi 连接成功后调用
 */
esp_err_t http_upload_init(void);

/**
 * @brief  周期上传：上传一条 IMU 数据到 VPS 后端
 *         由 upload_task 调用（独占 s_client）
 */
esp_err_t http_upload_imu_data(const char *device_id,
                               const qma7981_data_t *data,
                               int64_t timestamp);

/**
 * @brief  轮询后端是否有待执行的采集指令
 *         由 cmd_poll_task 调用（独占 s_poll_client）
 * @param  out_request_id   输出缓冲区，存放 request_id（建议 ≥48 字节）
 * @param  rid_size         缓冲区大小
 * @param  out_periodic_report  输出：后端要求的周期上报状态
 * @return ESP_OK  成功拉取到一条任务（out_request_id 已填充）
 *         ESP_ERR_NOT_FOUND  没有待执行任务（task=null）
 *         其他错误码  网络或解析失败
 */
esp_err_t http_poll_pending_task(char *out_request_id, int rid_size,
                                 bool *out_periodic_report);

/**
 * @brief  上报采集成功结果
 *         由 cmd_poll_task 调用（独占 s_report_client）
 */
esp_err_t http_report_complete(const char *request_id,
                               const qma7981_data_t *data,
                               int64_t timestamp);

/**
 * @brief  上报采集失败
 *         由 cmd_poll_task 调用（独占 s_report_client）
 */
esp_err_t http_report_fail(const char *request_id, const char *error);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_UPLOAD_H */