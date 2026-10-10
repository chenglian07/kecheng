/*
 * http_upload.c - HTTP 数据上传 & 远程任务通信
 *
 * 第2周增量：
 *   - POST /api/ingest  上传数据（修复字段名匹配后端）
 *   - GET  /api/commands/{device_id}/pending  轮询待处理命令
 *   - PUT  /api/commands/{request_id}  更新命令状态
 *   - 上传时可选绑定 request_id
 *
 * POST /api/ingest JSON 格式：
 * {
 *   "device_id": "group_01",
 *   "sensor": "qma7981",
 *   "ts_device": 1700000000.0,
 *   "ax": 0.0125,     // 单位 g
 *   "ay": -0.0342,
 *   "az": 0.9876,
 *   "unit": "g",
 *   "raw": [102, -280, 8081],
 *   "status": "ok",
 *   "request_id": "uuid-string" | null
 * }
 */

#include "http_upload.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HTTP_UPLOAD";

static char s_ingest_url[160];
static char s_cmd_base_url[160];
static esp_http_client_handle_t s_client = NULL;

/* ---------- 内部：构建 IMU JSON ---------- */

static char *build_imu_json(const char *device_id,
                            const qma7981_data_t *data,
                            int64_t timestamp,
                            const char *request_id)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddStringToObject(root, "sensor", "qma7981");
    cJSON_AddNumberToObject(root, "ts_device", (double)timestamp);
    cJSON_AddNumberToObject(root, "ax", data->ax_mg / 1000.0);
    cJSON_AddNumberToObject(root, "ay", data->ay_mg / 1000.0);
    cJSON_AddNumberToObject(root, "az", data->az_mg / 1000.0);
    cJSON_AddStringToObject(root, "unit", "g");
    cJSON *raw_arr = cJSON_CreateIntArray(
        (const int[]){data->raw_x, data->raw_y, data->raw_z}, 3);
    cJSON_AddItemToObject(root, "raw", raw_arr);
    cJSON_AddStringToObject(root, "status", "ok");
    if (request_id && request_id[0] != '\0') {
        cJSON_AddStringToObject(root, "request_id", request_id);
    } else {
        cJSON_AddNullToObject(root, "request_id");
    }
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

/* ---------- 内部：通用 HTTP 请求 ---------- */

static esp_err_t do_http_req(const char *url,
                              esp_http_client_method_t method,
                              const char *post_body,
                              int *out_status,
                              char **resp_body)
{
    if (!s_client) return ESP_FAIL;
    esp_http_client_set_url(s_client, url);
    esp_http_client_set_method(s_client, method);
    if (post_body) {
        esp_http_client_set_header(s_client, "Content-Type", "application/json");
        esp_http_client_set_post_field(s_client, post_body, strlen(post_body));
    } else {
        esp_http_client_set_post_field(s_client, NULL, 0);
    }
    esp_err_t err = esp_http_client_perform(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP fail [%s]: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(s_client);
        esp_http_client_config_t cfg = { .url = s_ingest_url, .timeout_ms = 5000, .buffer_size = 1024 };
        s_client = esp_http_client_init(&cfg);
        return err;
    }
    int status = esp_http_client_get_status_code(s_client);
    if (out_status) *out_status = status;
    if (resp_body) {
        int clen = esp_http_client_get_content_length(s_client);
        if (clen <= 0) clen = 2048;
        if (clen > 8192) clen = 8192;
        char *buf = malloc(clen + 1);
        if (buf) {
            int rlen = esp_http_client_read_response(s_client, buf, clen);
            if (rlen > 0) { buf[rlen] = '\0'; *resp_body = buf; }
            else { free(buf); *resp_body = NULL; }
        }
    }
    return ESP_OK;
}

/* ---------- 公开 API ---------- */

esp_err_t http_upload_init(void)
{
    snprintf(s_ingest_url, sizeof(s_ingest_url), "%s/api/ingest", CONFIG_SERVER_URL);
    snprintf(s_cmd_base_url, sizeof(s_cmd_base_url), "%s/api/commands", CONFIG_SERVER_URL);
    ESP_LOGI(TAG, "Ingest URL: %s", s_ingest_url);
    ESP_LOGI(TAG, "Commands URL: %s", s_cmd_base_url);
    if (s_client) { esp_http_client_cleanup(s_client); s_client = NULL; }
    esp_http_client_config_t config = { .url = s_ingest_url, .timeout_ms = 5000, .buffer_size = 1024 };
    s_client = esp_http_client_init(&config);
    if (!s_client) { ESP_LOGE(TAG, "HTTP init failed"); return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t http_upload_imu_data(const char *device_id,
                               const qma7981_data_t *data,
                               int64_t timestamp)
{
    return http_upload_imu_with_request_id(device_id, data, timestamp, NULL);
}

esp_err_t http_upload_imu_with_request_id(const char *device_id,
                                          const qma7981_data_t *data,
                                          int64_t timestamp,
                                          const char *request_id)
{
    char *json_str = build_imu_json(device_id, data, timestamp, request_id);
    if (!json_str) { ESP_LOGE(TAG, "JSON fail"); return ESP_FAIL; }
    ESP_LOGD(TAG, "Upload: %s", json_str);
    int status = 0;
    esp_err_t err = do_http_req(s_ingest_url, HTTP_METHOD_POST, json_str, &status, NULL);
    free(json_str);
    if (err == ESP_OK && (status == 200 || status == 201)) {
        ESP_LOGD(TAG, "Upload OK (HTTP %d)", status);
        return ESP_OK;
    }
    if (err == ESP_OK) ESP_LOGW(TAG, "Server HTTP %d", status);
    return ESP_FAIL;
}

esp_err_t http_poll_pending_commands(const char *device_id, char **out_json)
{
    if (!out_json) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;
    char url[256];
    snprintf(url, sizeof(url), "%s/%s/pending", s_cmd_base_url, device_id);
    int status = 0;
    esp_err_t err = do_http_req(url, HTTP_METHOD_GET, NULL, &status, out_json);
    if (err != ESP_OK) return err;
    if (status != 200) {
        if (*out_json) { free(*out_json); *out_json = NULL; }
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t http_update_command_status(const char *request_id, const char *status_str)
{
    char url[320];
    snprintf(url, sizeof(url), "%s/%s", s_cmd_base_url, request_id);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", status_str);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_FAIL;
    ESP_LOGI(TAG, "Update status: %s -> %s", request_id, status_str);
    int status = 0;
    esp_err_t err = do_http_req(url, HTTP_METHOD_PUT, body, &status, NULL);
    free(body);
    if (err == ESP_OK && status == 200) return ESP_OK;
    ESP_LOGW(TAG, "Update fail (HTTP %d)", status);
    return ESP_FAIL;
}