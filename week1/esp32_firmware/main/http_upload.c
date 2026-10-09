/*
 * http_upload.c - 通过 HTTP POST 将 IMU 数据上传到 VPS FastAPI 后端
 *
 * 优化：使用持久 HTTP 客户端连接（keep-alive），避免每次上传重建 TCP。
 *
 * POST /api/imu
 * Body JSON:
 * {
 *   "device_id": "group_01",
 *   "timestamp": 1700000000,
 *   "ax_mg": 12.5,
 *   "ay_mg": -34.2,
 *   "az_mg": 987.6,
 *   "raw_x": 102,
 *   "raw_y": -280,
 *   "raw_z": 8081,
 *   "chip_id": 179
 * }
 */

#include "http_upload.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "HTTP_UPLOAD";

static char s_upload_url[128];
static esp_http_client_handle_t s_client = NULL;

esp_err_t http_upload_init(void)
{
    snprintf(s_upload_url, sizeof(s_upload_url), "%s/api/imu", CONFIG_SERVER_URL);
    ESP_LOGI(TAG, "上传目标 URL: %s", s_upload_url);

    /* 创建持久 HTTP 客户端（后续请求复用连接） */
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
    esp_http_client_config_t config = {
        .url = s_upload_url,
        .timeout_ms = 3000,
    };
    s_client = esp_http_client_init(&config);
    if (!s_client) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t http_upload_imu_data(const char *device_id,
                               const qma7981_data_t *data,
                               int64_t timestamp)
{
    if (!s_client) {
        ESP_LOGE(TAG, "HTTP 客户端未初始化");
        return ESP_FAIL;
    }

    /* 构造 JSON */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "timestamp", (double)timestamp);
    cJSON_AddNumberToObject(root, "ax_mg", data->ax_mg);
    cJSON_AddNumberToObject(root, "ay_mg", data->ay_mg);
    cJSON_AddNumberToObject(root, "az_mg", data->az_mg);
    cJSON_AddNumberToObject(root, "raw_x", data->raw_x);
    cJSON_AddNumberToObject(root, "raw_y", data->raw_y);
    cJSON_AddNumberToObject(root, "raw_z", data->raw_z);
    cJSON_AddNumberToObject(root, "chip_id", data->chip_id);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "JSON 序列化失败");
        return ESP_FAIL;
    }

    int len = strlen(json_str);
    ESP_LOGD(TAG, "上传数据: %s", json_str);

    /* 复用连接发送 POST */
    esp_http_client_set_method(s_client, HTTP_METHOD_POST);
    esp_http_client_set_header(s_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(s_client, json_str, len);

    esp_err_t err = esp_http_client_perform(s_client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(s_client);
        if (status == 200 || status == 201) {
            ESP_LOGD(TAG, "✅ 上传成功 (HTTP %d)", status);
        } else {
            ESP_LOGW(TAG, "⚠️ 服务器返回 HTTP %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "❌ HTTP 请求失败: %s", esp_err_to_name(err));
        /* 连接可能已断开，重新初始化客户端 */
        esp_http_client_cleanup(s_client);
        esp_http_client_config_t config = {
            .url = s_upload_url,
            .timeout_ms = 3000,
        };
        s_client = esp_http_client_init(&config);
    }

    free(json_str);
    return err;
}