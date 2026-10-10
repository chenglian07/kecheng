/*
 * http_upload.h - HTTP 数据上传 & 远程任务通信模块
 *
 * 第1周：上传 IMU 数据
 * 第2周增量：
 *   - 修复上传格式匹配后端 /api/ingest
 *   - 新增 poll_pending_commands() 轮询待处理命令
 *   - 新增 update_command_status() 上报命令状态
 *   - 新增 upload_imu_with_request_id() 绑定远程采集 ID
 */

#ifndef HTTP_UPLOAD_H
#define HTTP_UPLOAD_H

#include "esp_err.h"
#include "qma7981.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 HTTP 客户端（同时准备 ingest 和 commands 两组 URL）
 */
esp_err_t http_upload_init(void);

/**
 * @brief  上传一条 IMU 数据到后端（不含 request_id，周期上报用）
 * @param  device_id  设备 ID 字符串
 * @param  data       QMA7981 加速度数据
 * @param  timestamp  Unix 时间戳（秒）
 * @return ESP_OK 上传成功
 */
esp_err_t http_upload_imu_data(const char *device_id,
                               const qma7981_data_t *data,
                               int64_t timestamp);

/**
 * @brief  上传 IMU 数据并绑定 request_id（远程采集用）
 *         后端会自动将对应命令状态置为 completed
 */
esp_err_t http_upload_imu_with_request_id(const char *device_id,
                                          const qma7981_data_t *data,
                                          int64_t timestamp,
                                          const char *request_id);

/**
 * @brief  轮询后端获取待处理命令
 * @param  device_id   设备 ID
 * @param  out_json    输出：malloc 分配的 JSON 字符串（调用者需 free）
 * @return ESP_OK 成功（out_json 可能为空数组 "[]"）
 */
esp_err_t http_poll_pending_commands(const char *device_id,
                                     char **out_json);

/**
 * @brief  更新命令状态（received / completed / failed）
 * @param  request_id  命令唯一 ID
 * @param  status      目标状态字符串
 * @return ESP_OK 成功
 */
esp_err_t http_update_command_status(const char *request_id,
                                     const char *status);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_UPLOAD_H */