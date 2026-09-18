#include "http_get.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http_get";

esp_err_t http_get_text(const char *url, char **out_buf, size_t *out_len)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "http client init failed");
        return ESP_FAIL;
    }

    /* 打开连接 */
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    /* 读取响应头长度 */
    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGD(TAG, "GET %s -> content_length=%d", url, content_length);

    /* 初始缓冲区大小 */
    size_t buf_size = (content_length > 0) ? (size_t)(content_length + 1) : 256;
    char *buf = (char *)malloc(buf_size);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    size_t total_read = 0;
    int read_len;

    while (1) {
        size_t remaining = buf_size - total_read - 1;
        if (remaining < 64) {
            buf_size *= 2;
            char *new_buf = (char *)realloc(buf, buf_size);
            if (!new_buf) {
                free(buf);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            buf = new_buf;
            remaining = buf_size - total_read - 1;
        }

        read_len = esp_http_client_read(client, buf + total_read, (int)remaining);
        if (read_len <= 0) {
            break;
        }
        total_read += (size_t)read_len;
    }

    buf[total_read] = '\0';
    *out_buf = buf;
    *out_len = total_read;

    /* 检查 HTTP 状态码 */
    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "HTTP GET %s -> %d", url, status);
        free(buf);
        *out_buf = NULL;
        *out_len = 0;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "GET %s -> %d (%zu bytes)", url, status, total_read);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}