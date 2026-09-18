#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qma7981.h"

/* secrets.h 由使用者从 secrets_template.h 复制并填充 */
#include "secrets.h"

static const char *TAG = "main";

/* ---------- 串口 JSON 输出 ---------- */

/**
 * JSON 包含字段：
 *   device_id  — 设备标识
 *   sensor     — 传感器型号
 *   ts_device  — 板端时间戳（秒，自启动以来）
 *   ax/ay/az   — 三轴加速度（单位：g）
 *   unit       — 单位
 *   raw        — 14-bit 原始值数组 [x, y, z]
 *   status     — 状态："ok" / "read_error"
 */
static void print_json(const qma7981_accel_t *accel, const char *status)
{
    double ts_device = (double)esp_timer_get_time() / 1.0e6;

    printf("{\"device_id\":\"%s\","
           "\"sensor\":\"qma7981\","
           "\"ts_device\":%.3f,"
           "\"ax\":%.4f,\"ay\":%.4f,\"az\":%.4f,"
           "\"unit\":\"g\","
           "\"raw\":[%d,%d,%d],"
           "\"status\":\"%s\"}\n",
           DEVICE_ID,
           ts_device,
           (double)accel->ax,
           (double)accel->ay,
           (double)accel->az,
           accel->raw_x,
           accel->raw_y,
           accel->raw_z,
           status);
}

/* ---------- 主程序 ---------- */

void app_main(void)
{
    ESP_LOGI(TAG, "=== KECHENG Week1 Phase1 ===");
    ESP_LOGI(TAG, "device_id = %s", DEVICE_ID);
    ESP_LOGI(TAG, "I2C: SDA=GPIO4  SCL=GPIO5  target addr=0x%02X", QMA7981_ADDR);

    /* ---- 1. 初始化 I2C 总线 + QMA7981 ---- */
    esp_err_t ret = qma7981_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "qma7981_init failed, aborting");
        return;
    }

    /* ---- 2. I2C 扫描（基于 i2c_master_probe） ---- */
    qma7981_i2c_scan();

    /* ---- 3. 读取 Chip ID ---- */
    uint8_t chip_id = 0;
    ret = qma7981_get_chip_id(&chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read CHIP_ID failed: %s", esp_err_to_name(ret));
        return;
    }

    if (chip_id == QMA7981_CHIP_ID_VAL) {
        ESP_LOGI(TAG, "QMA7981 CHIP_ID = 0x%02X (matched)", chip_id);
    } else if (chip_id == 0x00 || chip_id == 0xFF) {
        ESP_LOGW(TAG, "QMA7981 CHIP_ID = 0x%02X (bus may be faulty)", chip_id);
    } else {
        ESP_LOGI(TAG, "QMA7981 CHIP_ID = 0x%02X (unexpected, but non-zero)", chip_id);
    }

    /* ---- 4. 循环读取加速度并输出 JSON ---- */
    ESP_LOGI(TAG, "--- start periodic reading (1s interval) ---");
    vTaskDelay(pdMS_TO_TICKS(100)); /* 跳过启动不稳定读数 */

    while (1) {
        qma7981_accel_t accel;
        ret = qma7981_read_accel(&accel);

        if (ret == ESP_OK) {
            print_json(&accel, "ok");
        } else {
            qma7981_accel_t dummy = {0};
            print_json(&dummy, "read_error");
            ESP_LOGE(TAG, "read error: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}