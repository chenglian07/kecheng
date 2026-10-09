/*
 * main.c - 第1周：ESP32-S3-EYE + QMA7981 IMU 采集 → WiFi → HTTP 上传
 *
 * 工作流程：
 *   1. 初始化 I2C，读取 QMA7981 加速度计
 *   2. 连接 WiFi
 *   3. 同步 SNTP 时间（获取真实时间戳）
 *   4. 周期采集 IMU 数据 → POST 到 VPS 后端
 *
 * Kconfig 配置项（在 menuconfig 或 sdkconfig.defaults 中设置）：
 *   CONFIG_ESP_WIFI_SSID      - WiFi 名称
 *   CONFIG_ESP_WIFI_PASSWORD   - WiFi 密码
 *   CONFIG_SERVER_URL          - VPS 后端 URL，如 http://1.2.3.4:8000
 *   CONFIG_DEVICE_ID           - 本组设备 ID，如 group_01
 *   CONFIG_UPLOAD_INTERVAL_MS  - 上传周期（毫秒），默认 5000
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "qma7981.h"
#include "wifi_manager.h"
#include "http_upload.h"

static const char *TAG = "MAIN";

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

/* ---- Kconfig 选项声明（让 menuconfig 可见） ---- */

/* 在 sdkconfig.defaults 中已定义以下宏：
 *   CONFIG_ESP_WIFI_SSID
 *   CONFIG_ESP_WIFI_PASSWORD
 *   CONFIG_SERVER_URL
 *   CONFIG_DEVICE_ID
 *   CONFIG_UPLOAD_INTERVAL_MS
 */

/* ---- 主程序 ---- */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  第1周 IMU 采集系统启动");
    ESP_LOGI(TAG, "  设备ID: %s", CONFIG_DEVICE_ID);
    ESP_LOGI(TAG, "  目标服务器: %s", CONFIG_SERVER_URL);
    ESP_LOGI(TAG, "  上传周期: %d ms", CONFIG_UPLOAD_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");

    /* 1. 初始化 NVS（WiFi 需要） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. 初始化 QMA7981 加速度计 */
    ESP_LOGI(TAG, ">>> 初始化 QMA7981 传感器...");
    ret = qma7981_init(QMA7981_RANGE_8G);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ QMA7981 初始化失败！请检查接线：SDA=GPIO%d, SCL=GPIO%d, 地址=0x%02X",
                 QMA7981_I2C_SDA_GPIO, QMA7981_I2C_SCL_GPIO, QMA7981_I2C_ADDR);
        /* 不停止程序，进入调试循环 */
        while (1) {
            ESP_LOGE(TAG, "传感器未就绪，等待修复...");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    /* 测试读取一次 */
    qma7981_data_t test_data;
    ret = qma7981_read(&test_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 测试读取成功: raw_x=%d, raw_y=%d, raw_z=%d | ax=%.1f mg, ay=%.1f mg, az=%.1f mg",
                 test_data.raw_x, test_data.raw_y, test_data.raw_z,
                 test_data.ax_mg, test_data.ay_mg, test_data.az_mg);
    }

    /* 3. 连接 WiFi */
    ESP_LOGI(TAG, ">>> 连接 WiFi...");
    ret = wifi_manager_init();
    ESP_ERROR_CHECK(ret);

    ret = wifi_manager_wait_connect(30000);  /* 30秒超时 */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi 连接失败！请检查 SSID 和密码。将重启重试...");
        esp_restart();
    }
    ESP_LOGI(TAG, "✅ WiFi 已连接，IP: %s", wifi_manager_get_ip());

    /* 4. 同步时间 */
    init_sntp();

    /* 5. 初始化 HTTP 上传 */
    ESP_LOGI(TAG, ">>> 初始化 HTTP 上传...");
    ret = http_upload_init();
    ESP_ERROR_CHECK(ret);

    /* 6. 主循环：周期采集 + 上传 */
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  🚀 开始周期采集上传（每 %d ms）", CONFIG_UPLOAD_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");

    int upload_count = 0;
    int upload_fail_count = 0;

    while (1) {
        /* 采集 IMU 数据 */
        qma7981_data_t imu_data;
        ret = qma7981_read(&imu_data);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ 传感器读取失败");
            vTaskDelay(pdMS_TO_TICKS(CONFIG_UPLOAD_INTERVAL_MS));
            continue;
        }

        /* 获取时间戳 */
        int64_t ts = get_timestamp();

        /* 板端打印原始值 */
        ESP_LOGI(TAG, "[#%d] raw_x=%d, raw_y=%d, raw_z=%d | ax=%.1f mg, ay=%.1f mg, az=%.1f mg | ts=%lld",
                 upload_count,
                 imu_data.raw_x, imu_data.raw_y, imu_data.raw_z,
                 imu_data.ax_mg, imu_data.ay_mg, imu_data.az_mg,
                 (long long)ts);

        /* 检查 WiFi 连接 */
        if (!wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "⚠️ WiFi 未连接，等待重连...");
            vTaskDelay(pdMS_TO_TICKS(CONFIG_UPLOAD_INTERVAL_MS));
            continue;
        }

        /* 上传到 VPS */
        ret = http_upload_imu_data(CONFIG_DEVICE_ID, &imu_data, ts);
        if (ret == ESP_OK) {
            upload_count++;
            ESP_LOGI(TAG, "✅ 上传成功 (累计 %d 次)", upload_count);
        } else {
            upload_fail_count++;
            ESP_LOGW(TAG, "⚠️ 上传失败 (累计失败 %d 次)", upload_fail_count);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_UPLOAD_INTERVAL_MS));
    }
}