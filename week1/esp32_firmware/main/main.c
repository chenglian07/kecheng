/*
 * main.c - 第2周：ESP32-S3-EYE + QMA7981 IMU 采集 → WiFi → HTTP 上传 + 远程指令
 *
 * 第2周新增功能：
 *   - 双 Task 架构：upload_task（周期上报） + cmd_poll_task（指令拉取）
 *   - 事件组控制周期上报暂停/恢复
 *   - 远程采集指令接收、执行、结果回传
 *
 * 工作流程：
 *   1. 初始化 I2C，读取 QMA7981 加速度计
 *   2. 连接 WiFi
 *   3. 同步 SNTP 时间
 *   4. 初始化 3 个 HTTP 客户端
 *   5. 启动 upload_task（周期采集上传，可暂停）
 *   6. 启动 cmd_poll_task（每 500ms 拉取指令，永不停止）
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "qma7981.h"
#include "wifi_manager.h"
#include "http_upload.h"

static const char *TAG = "MAIN";

/* ---- 事件组：控制周期上报 ---- */
#define PERIODIC_REPORT_BIT  BIT0

static EventGroupHandle_t s_event_group = NULL;

/* ---- 指令拉取间隔 ---- */
#define CMD_POLL_INTERVAL_MS  500

/* ---- SNTP 时间同步 ---- */

static bool s_time_synced = false;

static void sntp_sync_callback(struct timeval *tv)
{
    s_time_synced = true;
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "✅ SNTP 时间已同步: %s", strftime_buf);
}

static void init_sntp(void)
{
    ESP_LOGI(TAG, "正在同步 SNTP 时间...");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.windows.com");
    sntp_set_time_sync_notification_cb(sntp_sync_callback);
    sntp_init();

    /* 等待时间同步，最多 30 秒 */
    for (int i = 0; i < 30 && !s_time_synced; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!s_time_synced) {
        ESP_LOGW(TAG, "⚠️ SNTP 同步超时，将使用启动以来的相对时间");
    }
}

static int64_t get_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec;
}

/* ---- 第2周：周期上传 Task（可通过事件组暂停/恢复） ---- */

static void upload_task(void *pvParameters)
{
    static const char *T = "UPLOAD";
    int upload_count = 0;
    int upload_fail_count = 0;

    ESP_LOGI(T, "upload_task 启动（周期 %d ms）", CONFIG_UPLOAD_INTERVAL_MS);

    while (1) {
        /* 检查事件组：PERIODIC_REPORT_BIT 置位 = 允许上报 */
        EventBits_t bits = xEventGroupGetBits(s_event_group);
        if (!(bits & PERIODIC_REPORT_BIT)) {
            /* 周期上报已暂停，静默等待 */
            vTaskDelay(pdMS_TO_TICKS(CONFIG_UPLOAD_INTERVAL_MS));
            continue;
        }

        /* 采集 IMU 数据 */
        qma7981_data_t imu_data;
        esp_err_t ret = qma7981_read(&imu_data);
        if (ret != ESP_OK) {
            ESP_LOGE(T, "❌ 传感器读取失败");
            vTaskDelay(pdMS_TO_TICKS(CONFIG_UPLOAD_INTERVAL_MS));
            continue;
        }

        int64_t ts = get_timestamp();

        ESP_LOGD(T, "[#%d] raw=%d/%d/%d | mg=%.1f/%.1f/%.1f | ts=%lld",
                 upload_count, imu_data.raw_x, imu_data.raw_y, imu_data.raw_z,
                 imu_data.ax_mg, imu_data.ay_mg, imu_data.az_mg, (long long)ts);

        if (!wifi_manager_is_connected()) {
            ESP_LOGW(T, "⚠️ WiFi 未连接，等待重连...");
            vTaskDelay(pdMS_TO_TICKS(CONFIG_UPLOAD_INTERVAL_MS));
            continue;
        }

        ret = http_upload_imu_data(CONFIG_DEVICE_ID, &imu_data, ts);
        if (ret == ESP_OK) {
            upload_count++;
            if (upload_count % 10 == 0) {
                ESP_LOGI(T, "✅ 已上传 %d 条数据", upload_count);
            }
        } else {
            upload_fail_count++;
            ESP_LOGW(T, "⚠️ 上传失败 (累计 %d 次)", upload_fail_count);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_UPLOAD_INTERVAL_MS));
    }
}

/* ---- 第2周：指令拉取 Task（每 500ms 轮询，永不停止） ---- */

static void cmd_poll_task(void *pvParameters)
{
    static const char *T = "CMD_POLL";
    char request_id[48];
    bool periodic_report = true;

    ESP_LOGI(T, "cmd_poll_task 启动（每 %d ms 轮询）", CMD_POLL_INTERVAL_MS);

    while (1) {
        if (!wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(CMD_POLL_INTERVAL_MS));
            continue;
        }

        esp_err_t err = http_poll_pending_task(
            request_id, sizeof(request_id), &periodic_report);

        if (err == ESP_OK) {
            /* ===== 拉取到新指令 ===== */
            ESP_LOGI(T, "📩 拉取到指令: %.12s..., periodic=%s",
                     request_id, periodic_report ? "true" : "false");

            /* 更新周期上报状态 */
            if (periodic_report) {
                xEventGroupSetBits(s_event_group, PERIODIC_REPORT_BIT);
            } else {
                xEventGroupClearBits(s_event_group, PERIODIC_REPORT_BIT);
                ESP_LOGI(T, "📢 后端要求: 暂停周期上报");
            }

            /* 执行一次采集 */
            qma7981_data_t imu_data;
            esp_err_t read_ret = qma7981_read(&imu_data);
            int64_t ts = get_timestamp();

            if (read_ret == ESP_OK) {
                ESP_LOGI(T, "🔬 单采完成: raw=%d/%d/%d | mg=%.1f/%.1f/%.1f",
                         imu_data.raw_x, imu_data.raw_y, imu_data.raw_z,
                         imu_data.ax_mg, imu_data.ay_mg, imu_data.az_mg);
                err = http_report_complete(request_id, &imu_data, ts);
                if (err == ESP_OK) {
                    ESP_LOGI(T, "✅ 结果已回传 (%.12s...)", request_id);
                } else {
                    ESP_LOGE(T, "❌ 回传失败: %s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGE(T, "❌ 传感器读取失败");
                http_report_fail(request_id, "sensor_read_failed");
            }
        } else if (err == ESP_ERR_NOT_FOUND) {
            /* 无新任务，但更新 periodic 状态 */
            if (periodic_report) {
                xEventGroupSetBits(s_event_group, PERIODIC_REPORT_BIT);
            } else {
                xEventGroupClearBits(s_event_group, PERIODIC_REPORT_BIT);
            }
        } else {
            ESP_LOGD(T, "拉取异常: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(CMD_POLL_INTERVAL_MS));
    }
}

/* ---- 主程序 ---- */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  第2周 IMU 采集+远程指令系统启动");
    ESP_LOGI(TAG, "  设备ID: %s", CONFIG_DEVICE_ID);
    ESP_LOGI(TAG, "  目标服务器: %s", CONFIG_SERVER_URL);
    ESP_LOGI(TAG, "  上传周期: %d ms", CONFIG_UPLOAD_INTERVAL_MS);
    ESP_LOGI(TAG, "  指令轮询: %d ms", CMD_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");

    /* 0. 创建事件组，初始默认允许周期上报 */
    s_event_group = xEventGroupCreate();
    xEventGroupSetBits(s_event_group, PERIODIC_REPORT_BIT);

    /* 1. 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. 初始化 QMA7981 */
    ESP_LOGI(TAG, ">>> 初始化 QMA7981 传感器...");
    ret = qma7981_init(QMA7981_RANGE_8G);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ QMA7981 初始化失败！SDA=GPIO%d, SCL=GPIO%d",
                 QMA7981_I2C_SDA_GPIO, QMA7981_I2C_SCL_GPIO);
        while (1) {
            ESP_LOGE(TAG, "传感器未就绪，等待修复...");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    if (!qma7981_is_active()) {
        ESP_LOGE(TAG, "❌ 传感器未能激活");
        ESP_LOGW(TAG, "   系统将上传零值数据");
    } else {
        ESP_LOGI(TAG, "✅ QMA7981 传感器正常");
    }

    /* 测试读取 */
    qma7981_data_t test_data;
    ret = qma7981_read(&test_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 测试读取: raw=%d/%d/%d | mg=%.1f/%.1f/%.1f",
                 test_data.raw_x, test_data.raw_y, test_data.raw_z,
                 test_data.ax_mg, test_data.ay_mg, test_data.az_mg);
    }

    /* 3. 连接 WiFi */
    ESP_LOGI(TAG, ">>> 连接 WiFi...");
    ret = wifi_manager_init();
    ESP_ERROR_CHECK(ret);
    ret = wifi_manager_wait_connect(30000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi 连接失败！重启重试...");
        esp_restart();
    }
    ESP_LOGI(TAG, "✅ WiFi 已连接，IP: %s", wifi_manager_get_ip());

    /* 4. 同步时间 */
    init_sntp();

    /* 5. 初始化 3 个 HTTP 客户端 */
    ESP_LOGI(TAG, ">>> 初始化 HTTP 客户端...");
    ret = http_upload_init();
    ESP_ERROR_CHECK(ret);

    /* 6. 启动双 Task */
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  🚀 upload_task  (周期 %d ms, 可暂停)", CONFIG_UPLOAD_INTERVAL_MS);
    ESP_LOGI(TAG, "  🚀 cmd_poll_task (轮询 %d ms, 永不停止)", CMD_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");

    xTaskCreate(upload_task,  "upload_task",  4096, NULL, 5, NULL);
    xTaskCreate(cmd_poll_task, "cmd_poll_task", 6144, NULL, 5, NULL);

    /* app_main 返回后 FreeRTOS 调度器接管 */
}