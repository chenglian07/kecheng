/*
 * http_upload.h - HTTP 数据上传模块
 */

#ifndef HTTP_UPLOAD_H
#define HTTP_UPLOAD_H

#include "esp_err.h"
#include "qma7981.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 HTTP 客户端
 */
esp_err_t http_upload_init(void);

/**
 * @brief  上传一条 IMU 数据到 VPS 后端
 * @param  device_id  设备 ID 字符串（如 "group_01"）
 * @param  data       QMA7981 加速度数据
 * @param  timestamp  Unix 时间戳（秒）
 * @return ESP_OK 上传成功
 */
esp_err_t http_upload_imu_data(const char *device_id,
                               const qma7981_data_t *data,
                               int64_t timestamp);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_UPLOAD_H */