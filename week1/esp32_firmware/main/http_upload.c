/*
 * http_upload.c - HTTP 数据上传 + 指令拉取 + 结果回传
 *
 * 第二周改造：3 个独立 HTTP 客户端实例，各自服务于一个 FreeRTOS 任务，无需互斥锁。
 *
 *   s_client        → upload_task 专用（POST /api/imu）
 *   s_poll_client   → cmd_poll_task 专用（GET /api/tasks/pending）
 *   s_report_client → cmd_poll_task 专用（POST /api/tasks/{id}/complete 或 /fail）
 */

#include "http_upload.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HTTP_UPLOAD";

/* ---- 3 个独立的持久 HTTP 客户端 ---- */
static char s_upload_url[128];
static char s_pending_url[160];
static char s_base_task_url[128];   /* 用于拼接 /api/tasks/{id}/complete */

static esp_http_client_handle_t s_client        = NULL;
static esp_http_client_handle_t s_poll_client   = NULL;
static esp_http_client_handle_t s_report_client = NULL;

/* 辅助：重建指定客户端 */
static esp_http_client_handle_t recreate_client(esp_http_client_handle_t old,
                                                 const char *url)
{
    if (old) esp_http_client_cleanup(old);
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
    };
    return esp_http_client_init(&config);
}

esp_err_t http_upload_init(void)
{
    snprintf(s_upload_url,    sizeof(s_upload_url),    "%s/api/imu",       CONFIG_SERVER_URL);
    snprintf(s_pending_url,   sizeof(s_pending_url),   "%s/api/tasks/pending?device_id=%s",
             CONFIG_SERVER_URL, CONFIG_DEVICE_ID);
    snprintf(s_base_task_url, sizeof(s_base_task_url),  "%s/api/tasks/",   CONFIG_SERVER_URL);

    ESP_LOGI(TAG, "上传 URL:    %s", s_upload_url);
    ESP_LOGI(TAG, "拉取 URL:    %s", s_pending_url);
    ESP_LOGI(TAG, "回传基础URL: %s", s_base_task_url);

    /* 客户端 1：周期上传（upload_task 专用） */
    s_client = recreate_client(s_client, s_upload_url);
    if (!s_client) { ESP_LOGE(TAG, "s_client 初始化失败"); return ESP_FAIL; }

    /* 客户端 2：指令拉取（cmd_poll_task 专用） */
    s_poll_client = recreate_client(s_poll_client, s_pending_url);
    if (!s_poll_client) { ESP_LOGE(TAG, "s_poll_client 初始化失败"); return ESP_FAIL; }

    /* 客户端 3：结果回传（cmd_poll_task 专用，URL 动态修改） */
    s_report_client = recreate_client(s_report_client, s_base_task_url);
    if (!s_report_client) { ESP_LOGE(TAG, "s_report_client 初始化失败"); return ESP_FAIL; }

    ESP_LOGI(TAG, "✅ 3 个 HTTP 客户端初始化完成");
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
    esp_http_client_set_url(s_client, s_upload_url);
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
        s_client = recreate_client(s_client, s_upload_url);
    }

    free(json_str);
    return err;
}

/* ================================================================
 *  http_poll_pending_task - 拉取待执行指令（cmd_poll_task 专用）
 *  GET /api/tasks/pending?device_id=xxx
 * ================================================================ */
esp_err_t http_poll_pending_task(char *out_request_id, int rid_size,
                                 bool *out_periodic_report)
{
    if (!s_poll_client) {
        ESP_LOGE(TAG, "s_poll_client 未初始化");
        return ESP_FAIL;
    }
    out_request_id[0] = '\0';
    *out_periodic_report = true;

    esp_http_client_set_url(s_poll_client, s_pending_url);
    esp_http_client_set_method(s_poll_client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_open(s_poll_client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "拉取指令 HTTP open 失败: %s", esp_err_to_name(err));
        s_poll_client = recreate_client(s_poll_client, s_pending_url);
        return err;
    }
    esp_http_client_fetch_headers(s_poll_client);
    int status = esp_http_client_get_status_code(s_poll_client);
    if (status != 200) {
        ESP_LOGW(TAG, "拉取指令返回 HTTP %d", status);
        esp_http_client_close(s_poll_client);
        return ESP_FAIL;
    }

    char resp_buf[512];
    int read_len = esp_http_client_read(s_poll_client, resp_buf, sizeof(resp_buf) - 1);
    esp_http_client_close(s_poll_client);

    if (read_len <= 0) {
        ESP_LOGW(TAG, "拉取指令响应为空");
        return ESP_FAIL;
    }
    resp_buf[read_len] = '\0';
    ESP_LOGD(TAG, "拉取响应: %s", resp_buf);

    cJSON *root = cJSON_Parse(resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON 解析失败");
        return ESP_FAIL;
    }

    cJSON *j_periodic = cJSON_GetObjectItem(root, "periodic_report");
    if (j_periodic && cJSON_IsBool(j_periodic)) {
        *out_periodic_report = cJSON_IsTrue(j_periodic);
    }

    cJSON *j_task = cJSON_GetObjectItem(root, "task");
    if (!j_task || cJSON_IsNull(j_task)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *j_rid = cJSON_GetObjectItem(j_task, "request_id");
    if (j_rid && cJSON_IsString(j_rid)) {
        strncpy(out_request_id, j_rid->valuestring, rid_size - 1);
        out_request_id[rid_size - 1] = '\0';
    } else {
        ESP_LOGE(TAG, "响应中缺少 request_id");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

/* ================================================================
 *  http_report_complete - 上报采集成功（cmd_poll_task 专用）
 *  POST /api/tasks/{request_id}/complete
 * ================================================================ */
esp_err_t http_report_complete(const char *request_id,
                               const qma7981_data_t *data,
                               int64_t timestamp)
{
    if (!s_report_client) {
        ESP_LOGE(TAG, "s_report_client 未初始化");
        return ESP_FAIL;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s%s/complete", s_base_task_url, request_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ax_mg", data->ax_mg);
    cJSON_AddNumberToObject(root, "ay_mg", data->ay_mg);
    cJSON_AddNumberToObject(root, "az_mg", data->az_mg);
    cJSON_AddNumberToObject(root, "raw_x", data->raw_x);
    cJSON_AddNumberToObject(root, "raw_y", data->raw_y);
    cJSON_AddNumberToObject(root, "raw_z", data->raw_z);
    cJSON_AddNumberToObject(root, "chip_id", data->chip_id);
    cJSON_AddNumberToObject(root, "timestamp", (double)timestamp);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_FAIL;

    int len = strlen(json_str);
    ESP_LOGD(TAG, "回传 complete: %s", json_str);

    esp_http_client_set_url(s_report_client, url);
    esp_http_client_set_method(s_report_client, HTTP_METHOD_POST);
    esp_http_client_set_header(s_report_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(s_report_client, json_str, len);

    esp_err_t err = esp_http_client_perform(s_report_client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(s_report_client);
        if (status != 200) {
            ESP_LOGW(TAG, "complete 返回 HTTP %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "complete HTTP 失败: %s", esp_err_to_name(err));
        s_report_client = recreate_client(s_report_client, s_base_task_url);
    }

    free(json_str);
    return err;
}

/* ================================================================
 *  http_report_fail - 上报采集失败（cmd_poll_task 专用）
 *  POST /api/tasks/{request_id}/fail
 * ================================================================ */
esp_err_t http_report_fail(const char *request_id, const char *error)
{
    if (!s_report_client) {
        ESP_LOGE(TAG, "s_report_client 未初始化");
        return ESP_FAIL;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s%s/fail", s_base_task_url, request_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", error);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_FAIL;

    int len = strlen(json_str);
    ESP_LOGD(TAG, "回传 fail: %s", json_str);

    esp_http_client_set_url(s_report_client, url);
    esp_http_client_set_method(s_report_client, HTTP_METHOD_POST);
    esp_http_client_set_header(s_report_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(s_report_client, json_str, len);

    esp_err_t err = esp_http_client_perform(s_report_client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(s_report_client);
        if (status != 200) {
            ESP_LOGW(TAG, "fail 返回 HTTP %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "fail HTTP 失败: %s", esp_err_to_name(err));
        s_report_client = recreate_client(s_report_client, s_base_task_url);
    }

    free(json_str);
    return err;
}