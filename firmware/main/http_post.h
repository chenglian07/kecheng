#ifndef HTTP_POST_H
#define HTTP_POST_H

#include "esp_err.h"

/**
 * @brief 向指定 URL 发送 JSON 数据（HTTP POST）
 * @param url    完整 URL，如 "http://10.1.41.154:8000/api/ingest"
 * @param json   JSON 字符串
 * @return ESP_OK 发送成功（HTTP 2xx），否则失败
 */
esp_err_t http_post_json(const char *url, const char *json);

#endif