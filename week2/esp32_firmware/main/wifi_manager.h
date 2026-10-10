/*
 * wifi_manager.h - WiFi 连接管理
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 WiFi 并连接到指定 AP
 *         使用 Kconfig 中的 SSID 和密码
 * @return ESP_OK 连接成功
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief  阻塞等待 WiFi 连接成功（带超时）
 * @param  timeout_ms 超时毫秒数
 * @return ESP_OK 已连接, ESP_ERR_TIMEOUT 超时
 */
esp_err_t wifi_manager_wait_connect(int timeout_ms);

/**
 * @brief  检查当前 WiFi 是否已连接
 */
bool wifi_manager_is_connected(void);

/**
 * @brief  获取当前 IP 地址字符串
 */
const char* wifi_manager_get_ip(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */