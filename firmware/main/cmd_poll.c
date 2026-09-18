#include "cmd_poll.h"
#include "http_get.h"
#include "http_post.h"
#include "qma7981.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

/* secrets.h 提供 DEVICE_ID 和 SERVER_BASE_URL */
#include "secrets.h"

static const char *TAG = "cmd_poll";

/* 周期上报暂停标志 */
static volatile bool s_reporting_paused = false;

bool cmd_is_reporting_paused(void)
{
    return s_reporting_paused;
}

/* ---------- 辅助函数 ---------- */

/**
 * 向服务器发送命令状态更新
 * PUT /api/commands/{request_id}  {"status": "received"}
 */
static esp_err_t send_command_ack(const char *request_id, const char *status)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/commands/%s", SERVER_BASE_URL, request_id);

    char body[128];
    snprintf(body, sizeof(body), "{\"status\":\"%s\"}", status);

    return http_post_json(url, body);
}

/**
 * 构造包含 request_id 的传感器 JSON 并上传
 */
static esp_err_t upload_with_request_id(const qma7981_accel_t *accel, const char *request_id)
{
    double ts_device = (double)esp_timer_get_time() / 1.0e6;
    char json[320];
    int n = snprintf(json, sizeof(json),
        "{\"device_id\":\"%s\","
        "\"sensor\":\"qma7981\","
        "\"ts_device\":%.3f,"
        "\"ax\":%.4f,\"ay\":%.4f,\"az\":%.4f,"
        "\"unit\":\"g\","
        "\"raw\":[%d,%d,%d],"
        "\"request_id\":\"%s\"}",
        DEVICE_ID, ts_device,
        (double)accel->ax, (double)accel->ay, (double)accel->az,
        accel->raw_x, accel->raw_y, accel->raw_z,
        request_id);

    if (n >= (int)sizeof(json)) {
        ESP_LOGW(TAG, "JSON buffer too small, truncated");
    }

    char url[160];
    snprintf(url, sizeof(url), "%s/api/ingest", SERVER_BASE_URL);

    return http_post_json(url, json);
}

/* ---------- 处理单条命令 ---------- */

static void handle_command(cJSON *cmd)
{
    cJSON *req_id_item = cJSON_GetObjectItem(cmd, "request_id");
    cJSON *type_item   = cJSON_GetObjectItem(cmd, "command_type");
    cJSON *params_item = cJSON_GetObjectItem(cmd, "params");

    if (!cJSON_IsString(req_id_item) || !cJSON_IsString(type_item)) {
        ESP_LOGW(TAG, "invalid command item (missing request_id or command_type)");
        return;
    }

    const char *request_id   = req_id_item->valuestring;
    const char *command_type = type_item->valuestring;

    ESP_LOGI(TAG, "processing command: type=%s id=%s", command_type, request_id);

    /* --- 采集一次 --- */
    if (strcmp(command_type, "collect_once") == 0) {
        // 1. 发送 received 回执
        esp_err_t ret = send_command_ack(request_id, "received");
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "send received ack failed for %s", request_id);
            // 继续执行
        }

        // 2. 读一次传感器
        vTaskDelay(pdMS_TO_TICKS(50));
        qma7981_accel_t accel;
        ret = qma7981_read_accel(&accel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "collect_once read failed: %s", esp_err_to_name(ret));
            send_command_ack(request_id, "failed");
            return;
        }

        // 3. 串口输出
        double ts_device = (double)esp_timer_get_time() / 1.0e6;
        printf("{\"device_id\":\"%s\","
               "\"sensor\":\"qma7981\","
               "\"ts_device\":%.3f,"
               "\"ax\":%.4f,\"ay\":%.4f,\"az\":%.4f,"
               "\"unit\":\"g\","
               "\"raw\":[%d,%d,%d],"
               "\"status\":\"collect_once\","
               "\"request_id\":\"%s\"}\n",
               DEVICE_ID, ts_device,
               (double)accel.ax, (double)accel.ay, (double)accel.az,
               accel.raw_x, accel.raw_y, accel.raw_z,
               request_id);

        // 4. 上传带 request_id 的数据
        ret = upload_with_request_id(&accel, request_id);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "upload collect_once result failed");
        }

    /* --- 设置周期上报 --- */
    } else if (strcmp(command_type, "set_reporting") == 0) {
        if (cJSON_IsString(params_item)) {
            const char *params_str = params_item->valuestring;
            cJSON *params = cJSON_Parse(params_str);
            if (params) {
                cJSON *enabled = cJSON_GetObjectItem(params, "enabled");
                if (cJSON_IsBool(enabled)) {
                    s_reporting_paused = !cJSON_IsTrue(enabled);
                    ESP_LOGI(TAG, "periodic reporting %s",
                             s_reporting_paused ? "PAUSED" : "RESUMED");
                }
                cJSON_Delete(params);
            }
        }
        // 标记命令完成
        send_command_ack(request_id, "completed");

    } else {
        ESP_LOGW(TAG, "unknown command type: %s", command_type);
        send_command_ack(request_id, "failed");
    }
}

/* ---------- 解析轮询响应 ---------- */

static void process_poll_response(const char *json_body)
{
    cJSON *root = cJSON_Parse(json_body);
    if (!root) {
        ESP_LOGW(TAG, "poll response JSON parse failed");
        return;
    }

    cJSON *code_item = cJSON_GetObjectItem(root, "code");
    if (!cJSON_IsNumber(code_item) || code_item->valueint != 0) {
        ESP_LOGW(TAG, "poll response code != 0");
        cJSON_Delete(root);
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return;
    }

    int count = cJSON_GetArraySize(data);
    if (count > 0) {
        ESP_LOGI(TAG, "received %d pending command(s)", count);
    }

    for (int i = 0; i < count; i++) {
        cJSON *cmd = cJSON_GetArrayItem(data, i);
        if (cmd) {
            handle_command(cmd);
        }
    }

    cJSON_Delete(root);
}

/* ---------- 轮询任务 ---------- */

static void poll_task(void *arg)
{
    (void)arg;
    char url[256];

    ESP_LOGI(TAG, "cmd_poll task started, device=%s", DEVICE_ID);

    while (1) {
        snprintf(url, sizeof(url), "%s/api/commands/%s/pending",
                 SERVER_BASE_URL, DEVICE_ID);

        char *response = NULL;
        size_t len = 0;

        esp_err_t ret = http_get_text(url, &response, &len);
        if (ret == ESP_OK && response) {
            process_poll_response(response);
            free(response);
        } else {
            ESP_LOGW(TAG, "poll request failed");
        }

        /* 每 3 秒轮询一次 */
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    vTaskDelete(NULL);
}

/* ---------- 公共接口 ---------- */

void cmd_poll_start(void)
{
    BaseType_t ret = xTaskCreate(
        poll_task,
        "cmd_poll",
        4096,   /* 栈大小 4KB */
        NULL,
        1,      /* 低优先级 */
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create cmd_poll task");
    } else {
        ESP_LOGI(TAG, "cmd_poll task created");
    }
}