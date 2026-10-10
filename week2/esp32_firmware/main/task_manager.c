/*
 * task_manager.c - 第2周：远程采集任务管理
 *
 * 工作流程：
 *   1. 每隔 TASK_POLL_INTERVAL_MS 轮询 GET /api/commands/{device_id}/pending
 *   2. 如果拿到 pending 命令：
 *      a. PUT /api/commands/{request_id} → status=received
 *      b. 执行一次真实 IMU 读取
 *      c. POST /api/ingest 上传数据，带 request_id
 *      d. 后端自动将命令状态更新为 completed
 *   3. 设置 busy 标志，主循环据此暂停周期上报
 */

#include "task_manager.h"
#include "http_upload.h"
#include "qma7981.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <sys/time.h>

static const char *TAG = "TASK_MGR";

static volatile bool s_busy = false;

/* ---------- 内部辅助 ---------- */

static int64_t get_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec;
}

/**
 * 处理一条远程采集命令
 */
static void handle_command(const char *request_id)
{
    ESP_LOGI(TAG, "📋 收到远程采集任务: request_id=%.8s...", request_id);

    /* 1. 标记 received */
    s_busy = true;
    esp_err_t ret = http_update_command_status(request_id, "received");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ 标记 received 失败，继续采集");
    }

    /* 2. 执行真实 IMU 读取 */
    qma7981_data_t imu_data;
    ret = qma7981_read(&imu_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ IMU 读取失败");
        http_update_command_status(request_id, "failed");
        s_busy = false;
        return;
    }

    int64_t ts = get_timestamp();
    ESP_LOGI(TAG, "✅ IMU 读取成功: ax=%.2f ay=%.2f az=%.2f mg, ts=%lld",
             imu_data.ax_mg, imu_data.ay_mg, imu_data.az_mg, (long long)ts);

    /* 3. 上传数据（绑定 request_id） */
    ret = http_upload_imu_with_request_id(CONFIG_DEVICE_ID, &imu_data, ts, request_id);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 数据已上传，绑定 request_id=%.8s...", request_id);
        /* 后端 ingest 端点会自动将命令状态置为 completed */
    } else {
        ESP_LOGE(TAG, "❌ 数据上传失败");
    }

    s_busy = false;
}

/* ---------- 轮询任务 ---------- */

static void task_manager_loop(void *arg)
{
    ESP_LOGI(TAG, "🔄 任务轮询已启动 (间隔 %d ms)", TASK_POLL_INTERVAL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TASK_POLL_INTERVAL_MS));

        /* WiFi 未连接则跳过 */
        if (!wifi_manager_is_connected()) {
            ESP_LOGD(TAG, "WiFi 未连接，跳过轮询");
            continue;
        }

        /* 轮询 pending 命令 */
        char *json_resp = NULL;
        esp_err_t ret = http_poll_pending_commands(CONFIG_DEVICE_ID, &json_resp);
        if (ret != ESP_OK || !json_resp) {
            ESP_LOGD(TAG, "轮询无响应或失败");
            continue;
        }

        /* 解析 JSON */
        cJSON *root = cJSON_Parse(json_resp);
        free(json_resp);
        json_resp = NULL;

        if (!root) {
            ESP_LOGW(TAG, "JSON 解析失败");
            continue;
        }

        cJSON *code = cJSON_GetObjectItem(root, "code");
        cJSON *data = cJSON_GetObjectItem(root, "data");

        if (!code || code->valueint != 0 || !data || !cJSON_IsArray(data)) {
            cJSON_Delete(root);
            continue;
        }

        int arr_size = cJSON_GetArraySize(data);
        if (arr_size == 0) {
            cJSON_Delete(root);
            continue;
        }

        /* 取第一条 pending 命令 */
        cJSON *cmd = cJSON_GetArrayItem(data, 0);
        cJSON *req_id = cJSON_GetObjectItem(cmd, "request_id");

        if (req_id && cJSON_IsString(req_id) && req_id->valuestring[0] != '\0') {
            handle_command(req_id->valuestring);
        }

        cJSON_Delete(root);
    }
}

/* ---------- 公开 API ---------- */

esp_err_t task_manager_start(void)
{
    BaseType_t xret = xTaskCreate(
        task_manager_loop,
        "task_mgr",
        4096,       /* 栈大小 */
        NULL,
        5,          /* 优先级（高于主循环） */
        NULL
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "❌ 创建任务管理 Task 失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool task_manager_is_busy(void)
{
    return s_busy;
}