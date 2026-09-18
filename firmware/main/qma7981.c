#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qma7981.h"

static const char *TAG = "qma7981";

/* ESP-IDF v5.x 新 I2C Master 驱动句柄 */
static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;

/* ---------- I2C 读写（新 API） ---------- */

static esp_err_t qma7981_reg_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
    /* transmit_receive：先发送 reg_addr，再接收 len 字节 */
    return i2c_master_transmit_receive(s_dev_handle, &reg_addr, 1,
                                       data, len, pdMS_TO_TICKS(100));
}

static esp_err_t qma7981_reg_write(uint8_t reg_addr, uint8_t data)
{
    if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg_addr, data};
    return i2c_master_transmit(s_dev_handle, buf, sizeof(buf),
                               pdMS_TO_TICKS(100));
}

/* ---------- 初始化 ---------- */

esp_err_t qma7981_init(void)
{
    ESP_LOGI(TAG, "I2C master init (SDA=GPIO%d, SCL=GPIO%d, %d Hz)",
             QMA7981_I2C_SDA, QMA7981_I2C_SCL, QMA7981_I2C_FREQ_HZ);

    /* 1. 创建 I2C 主总线 */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,                         /* -1 = 自动分配 */
        .sda_io_num = QMA7981_I2C_SDA,
        .scl_io_num = QMA7981_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 向总线添加 QMA7981 设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMA7981_ADDR,
        .scl_speed_hz = QMA7981_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. 设置 Active 模式（唤醒传感器） */
    ret = qma7981_reg_write(QMA7981_REG_PWR_MGMT, QMA7981_PWR_ACTIVE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write PWR_MGMT failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "QMA7981 initialized (active mode)");
    return ESP_OK;
}

/* ---------- Chip ID ---------- */

esp_err_t qma7981_get_chip_id(uint8_t *chip_id)
{
    if (!chip_id) return ESP_ERR_INVALID_ARG;
    return qma7981_reg_read(QMA7981_REG_CHIP_ID, chip_id, 1);
}

/* ---------- 读取加速度 ---------- */

esp_err_t qma7981_read_accel(qma7981_accel_t *accel)
{
    if (!accel) return ESP_ERR_INVALID_ARG;

    uint8_t buf[6]; /* X_LSB ~ Z_MSB */
    esp_err_t ret = qma7981_reg_read(QMA7981_REG_ACC_X_LSB, buf, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    /*
     * 数据格式（14-bit，小端序）：
     *   LSB  [7:2] = 加速数据低 6 位  D5..D0
     *   LSB  [1:0] = NEW_DATA 状态位  （正常为 01，表示新数据就绪）
     *   MSB  [7:0] = 加速数据高 8 位  D13..D6
     *
     * 组合（掩码掉状态位后再对齐）：
     *   raw_signed = ((MSB << 8) | (LSB & 0xFC)) / 4
     *
     * 换算（±2g 默认量程）：
     *   accel_g = raw * 2.0 / 8191
     */

    int16_t raw_x = (int16_t)((buf[0] & 0xFC) | ((uint16_t)buf[1] << 8)) / 4;
    int16_t raw_y = (int16_t)((buf[2] & 0xFC) | ((uint16_t)buf[3] << 8)) / 4;
    int16_t raw_z = (int16_t)((buf[4] & 0xFC) | ((uint16_t)buf[5] << 8)) / 4;

    accel->raw_x = raw_x;
    accel->raw_y = raw_y;
    accel->raw_z = raw_z;

    accel->ax = (float)raw_x * QMA7981_RANGE_G / QMA7981_MAX_14BIT;
    accel->ay = (float)raw_y * QMA7981_RANGE_G / QMA7981_MAX_14BIT;
    accel->az = (float)raw_z * QMA7981_RANGE_G / QMA7981_MAX_14BIT;

    return ESP_OK;
}

/* ---------- I2C 总线扫描 ---------- */

void qma7981_i2c_scan(void)
{
    if (!s_bus_handle) {
        ESP_LOGE(TAG, "bus not initialized, cannot scan");
        return;
    }

    ESP_LOGI(TAG, "=== I2C bus scan ===");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        /* i2c_master_probe 发送 START + 地址位，等待 ACK */
        esp_err_t ret = i2c_master_probe(s_bus_handle, addr, pdMS_TO_TICKS(50));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device found at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "scan complete, %d device(s) found", found);
}