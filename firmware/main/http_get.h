#ifndef HTTP_GET_H
#define HTTP_GET_H

#include "esp_err.h"

/**
 * @brief 向指定 URL 发送 GET 请求，读取响应正文文本
 * @param url      完整 URL
 * @param out_buf  输出：响应的字符串（需调用者 free()）
 * @param out_len  输出：响应长度（不含 '\0'）
 * @return ESP_OK 成功
 */
esp_err_t http_get_text(const char *url, char **out_buf, size_t *out_len);

#endif