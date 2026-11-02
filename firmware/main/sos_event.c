/**
 * Week 3: SOS 求助事件状态机
 *
 * 核心逻辑：
 *   1. 按键触发 → local_ack（立即，不依赖网络）
 *   2. 后台任务尝试上传 → uploading → delivered / failed
 *   3. delivered 后轮询服务端回应/取消
 *   4. cancelled 为终态，不可被 acknowledged 覆盖
 */

#include "sos_event.h"
#include "sos_feedback.h"
#include "http_post.h"
#include "http_get.h"
#include "wifi_conn.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#include "secrets.h"

static const char *TAG = "sos_evt";

/* 当前事件（受互斥锁保护） */
static sos_event_t s_current_event;
static SemaphoreHandle_t s_mutex = NULL;

/* 上传信号量 */
static SemaphoreHandle_t s_upload_sem = NULL;

/* 上传任务句柄 */
static TaskHandle_t s_task_handle = NULL;

/* ----- 辅助函数 ----- */

static double get_board_time_sec(void)
{
    return (double)esp_timer_get_time() / 1.0e6;
}

static void generate_event_id(char *buf, size_t len)
{
    /* 格式: SOS-<板端时间ms>-<随机4位hex> */
    int64_t us = esp_timer_get_time();
    uint32_t rnd = esp_random();
    snprintf(buf, len, "SOS-%08llx-%04x",
             (unsigned long long)(us / 1000), rnd & 0xFFFF);
}

static bool is_terminal_state(sos_state_t state)
{
    return (state == SOS_STATE_CANCELLED ||
            state == SOS_STATE_FAILED ||
            state == SOS_STATE_TIMEOUT ||
            state == SOS_STATE_ACKNOWLEDGED);
}

/**
 * 向服务器发送求助事件
 * POST /api/help_events
 */
static esp_err_t upload_help_event(const sos_event_t *evt)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/help_events", SERVER_BASE_URL);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event_id", evt->event_id);
    cJSON_AddStringToObject(root, "device_id", DEVICE_ID);
    cJSON_AddStringToObject(root, "source", evt->source);
    cJSON_AddNumberToObject(root, "ts_created", evt->ts_created);
    cJSON_AddNumberToObject(root, "ts_local_ack", evt->ts_local_ack);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json) {
        ESP_LOGE(TAG, "JSON creation failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "uploading event: %s", json);
    esp_err_t ret = http_post_json(url, json);
    free(json);
    return ret;
}

/**
 * 轮询服务端回应/取消
 * GET /api/help_events/{event_id}
 */
static esp_err_t poll_event_status(const char *event_id, sos_event_t *out_updated)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/help_events/%s", SERVER_BASE_URL, event_id);

    char *response = NULL;
    size_t len = 0;
    esp_err_t ret = http_get_text(url, &response, &len);

    if (ret != ESP_OK || !response) {
        ESP_LOGW(TAG, "poll event status failed");
        return ESP_FAIL;

/* ----- 上传任务 ----- */

static void upload_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "SOS upload task started");

    while (1) {
        /* 等待上传信号 */
        if (xSemaphoreTake(s_upload_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "upload task triggered");

        sos_event_t evt;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memcpy(&evt, &s_current_event, sizeof(evt));
        xSemaphoreGive(s_mutex);

        if (evt.state != SOS_STATE_LOCAL_ACK) {
            ESP_LOGW(TAG, "event not in local_ack state, skip upload");
            continue;
        }

        /* 检查 WiFi */
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "WiFi not connected, cannot upload. "
                     "Event stays in local_ack state.");
            sos_feedback_update(SOS_FB_FAILED, evt.event_id,
                                "reason=wifi_disconnected");
            continue;
        }

        /* 进入 uploading 状态 */
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_current_event.state = SOS_STATE_UPLOADING;
        memcpy(&evt, &s_current_event, sizeof(evt));
        xSemaphoreGive(s_mutex);

        sos_feedback_update(SOS_FB_UPLOADING, evt.event_id, NULL);

        /* 尝试上传 */
        int retry = 0;
        bool uploaded = false;

        while (retry < SOS_UPLOAD_MAX_RETRY && !uploaded) {
            if (!wifi_is_connected()) {
                ESP_LOGW(TAG, "WiFi lost during upload");
                break;
            }

            esp_err_t ret = upload_help_event(&evt);
            if (ret == ESP_OK) {
                uploaded = true;
                ESP_LOGI(TAG, "upload success");
            } else {
                retry++;
                ESP_LOGW(TAG, "upload failed, retry %d/%d",
                         retry, SOS_UPLOAD_MAX_RETRY);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }

        if (uploaded) {
            /* 进入 delivered */
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_current_event.state = SOS_STATE_DELIVERED;
            s_current_event.ts_server_received = get_board_time_sec();
            s_current_event.upload_retry_count = retry;
            memcpy(&evt, &s_current_event, sizeof(evt));
            xSemaphoreGive(s_mutex);

            sos_feedback_update(SOS_FB_DELIVERED, evt.event_id,
                                "server_received=yes");

            /* 轮询回应/取消 */
            ESP_LOGI(TAG, "polling for viewer response...");
            int poll_count = 0;
            const int max_polls = 20;  /* 最多 60 秒 */

            while (poll_count < max_polls) {
                vTaskDelay(pdMS_TO_TICKS(SOS_RESPONSE_POLL_MS));

                if (!wifi_is_connected()) {
                    ESP_LOGW(TAG, "WiFi lost during polling");
                    break;
                }

                sos_event_t updated = evt;
                esp_err_t ret = poll_event_status(evt.event_id, &updated);

                if (ret == ESP_OK) {
                    if (updated.state == SOS_STATE_ACKNOWLEDGED) {
                        xSemaphoreTake(s_mutex, portMAX_DELAY);
                        s_current_event.state = SOS_STATE_ACKNOWLEDGED;
                        s_current_event.ts_viewer_ack = get_board_time_sec();
                        xSemaphoreGive(s_mutex);

                        sos_feedback_update(SOS_FB_ACKNOWLEDGED,
                                            evt.event_id, "viewer_responded=yes");
                        ESP_LOGI(TAG, "event acknowledged by viewer");
                        break;
                    } else if (updated.state == SOS_STATE_CANCELLED) {
                        xSemaphoreTake(s_mutex, portMAX_DELAY);
                        s_current_event.state = SOS_STATE_CANCELLED;
                        s_current_event.ts_cancelled = get_board_time_sec();
                        xSemaphoreGive(s_mutex);

                        sos_feedback_update(SOS_FB_CANCELLED,
                                            evt.event_id, "cancelled_by=viewer");
                        ESP_LOGI(TAG, "event cancelled by viewer");
                        break;
                    }
                }
                poll_count++;
            }

            if (poll_count >= max_polls) {
                ESP_LOGW(TAG, "no viewer response within polling window");
            }

        } else {
            /* 上传失败 */
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_current_event.state = SOS_STATE_FAILED;
            s_current_event.upload_retry_count = retry;
            memcpy(&evt, &s_current_event, sizeof(evt));
            xSemaphoreGive(s_mutex);

            sos_feedback_update(SOS_FB_FAILED, evt.event_id,
                                "reason=upload_failed");
            ESP_LOGE(TAG, "upload failed after %d retries", retry);

/* ----- 公共接口 ----- */

esp_err_t sos_event_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "mutex creation failed");
        return ESP_FAIL;
    }

    s_upload_sem = xSemaphoreCreateBinary();
    if (!s_upload_sem) {
        ESP_LOGE(TAG, "semaphore creation failed");
        vSemaphoreDelete(s_mutex);
        return ESP_FAIL;
    }

    memset(&s_current_event, 0, sizeof(s_current_event));
    s_current_event.state = SOS_STATE_IDLE;

    BaseType_t ret = xTaskCreate(
        upload_task,
        "sos_upload",
        4096,
        NULL,
        2,
        &s_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "upload task creation failed");
        vSemaphoreDelete(s_mutex);
        vSemaphoreDelete(s_upload_sem);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SOS event module initialized");
    return ESP_OK;
}

void sos_event_trigger(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_current_event.state != SOS_STATE_IDLE &&
        !is_terminal_state(s_current_event.state)) {
        ESP_LOGW(TAG, "active event exists (state=%s), ignore new trigger",
                 sos_state_str(s_current_event.state));
        xSemaphoreGive(s_mutex);
        return;
    }

    memset(&s_current_event, 0, sizeof(s_current_event));
    generate_event_id(s_current_event.event_id, sizeof(s_current_event.event_id));
    s_current_event.state = SOS_STATE_LOCAL_ACK;
    s_current_event.ts_created = get_board_time_sec();
    s_current_event.ts_local_ack = s_current_event.ts_created;
    strncpy(s_current_event.source, "button", sizeof(s_current_event.source) - 1);
    s_current_event.ts_server_received = -1.0;
    s_current_event.ts_viewer_ack = -1.0;
    s_current_event.ts_cancelled = -1.0;

    sos_event_t evt;
    memcpy(&evt, &s_current_event, sizeof(evt));
    xSemaphoreGive(s_mutex);

    sos_feedback_update(SOS_FB_LOCAL_ACK, evt.event_id, "source=button");
    ESP_LOGI(TAG, "SOS event triggered: id=%s state=local_ack", evt.event_id);

    xSemaphoreGive(s_upload_sem);
}

void sos_event_handle_response(const char *event_id, const char *viewer_ack_at)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_current_event.state == SOS_STATE_CANCELLED) {
        ESP_LOGW(TAG, "event already cancelled, ignore response");
        xSemaphoreGive(s_mutex);
        return;
    }

    if (strcmp(s_current_event.event_id, event_id) != 0) {
        ESP_LOGW(TAG, "event_id mismatch, ignore");
        xSemaphoreGive(s_mutex);
        return;
    }

    if (s_current_event.state == SOS_STATE_DELIVERED) {
        s_current_event.state = SOS_STATE_ACKNOWLEDGED;
        s_current_event.ts_viewer_ack = get_board_time_sec();
        sos_event_t evt;
        memcpy(&evt, &s_current_event, sizeof(evt));
        xSemaphoreGive(s_mutex);

        sos_feedback_update(SOS_FB_ACKNOWLEDGED, evt.event_id,
                            "source=cmd_poll");
        ESP_LOGI(TAG, "event acknowledged via cmd_poll");
    } else {
        ESP_LOGW(TAG, "cannot acknowledge in state %s",
                 sos_state_str(s_current_event.state));
        xSemaphoreGive(s_mutex);
    }
}

void sos_event_handle_cancel(const char *event_id, const char *cancelled_at)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (strcmp(s_current_event.event_id, event_id) != 0) {
        ESP_LOGW(TAG, "event_id mismatch, ignore cancel");
        xSemaphoreGive(s_mutex);
        return;
    }

    if (is_terminal_state(s_current_event.state)) {
        ESP_LOGW(TAG, "event already in terminal state %s, ignore cancel",
                 sos_state_str(s_current_event.state));
        xSemaphoreGive(s_mutex);
        return;
    }

    s_current_event.state = SOS_STATE_CANCELLED;
    s_current_event.ts_cancelled = get_board_time_sec();
    sos_event_t evt;
    memcpy(&evt, &s_current_event, sizeof(evt));
    xSemaphoreGive(s_mutex);

    sos_feedback_update(SOS_FB_CANCELLED, evt.event_id, "source=cmd_poll");
    ESP_LOGI(TAG, "event cancelled via cmd_poll");
}

bool sos_event_get_current(sos_event_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool has_event = (s_current_event.state != SOS_STATE_IDLE);
    if (out && has_event) {
        memcpy(out, &s_current_event, sizeof(sos_event_t));
    }
    xSemaphoreGive(s_mutex);
    return has_event;
}

const char *sos_state_str(sos_state_t state)
{
    switch (state) {
        case SOS_STATE_IDLE:         return "idle";
        case SOS_STATE_LOCAL_ACK:    return "local_ack";
        case SOS_STATE_UPLOADING:    return "uploading";
        case SOS_STATE_DELIVERED:    return "delivered";
        case SOS_STATE_ACKNOWLEDGED: return "acknowledged";
        case SOS_STATE_CANCELLED:    return "cancelled";
        case SOS_STATE_FAILED:       return "failed";
        case SOS_STATE_TIMEOUT:      return "timeout";
        default:                     return "unknown";
    }
}
        }

        ESP_LOGI(TAG, "upload task cycle complete");
    }

    vTaskDelete(NULL);
}
    }

    cJSON *root = cJSON_Parse(response);
    free(response);
    if (!root) return ESP_FAIL;

    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (!cJSON_IsNumber(code) || code->valueint != 0 || !data) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *status = cJSON_GetObjectItem(data, "status");
    if (cJSON_IsString(status)) {
        const char *st = status->valuestring;
        if (strcmp(st, "acknowledged") == 0) {
            out_updated->state = SOS_STATE_ACKNOWLEDGED;
        } else if (strcmp(st, "cancelled") == 0) {
            out_updated->state = SOS_STATE_CANCELLED;
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}