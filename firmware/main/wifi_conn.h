#ifndef WIFI_CONN_H
#define WIFI_CONN_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief 初始化 WiFi 并等待连接成功（阻塞，最多等待 timeout_ms）
 * @return ESP_OK 连接成功，否则失败
 */
esp_err_t wifi_connect(uint32_t timeout_ms);

/**
 * @brief 断开 WiFi 连接
 */
void wifi_disconnect(void);

/**
 * @brief 获取当前连接状态
 * @return true 已连接，false 未连接
 */
bool wifi_is_connected(void);

#endif
