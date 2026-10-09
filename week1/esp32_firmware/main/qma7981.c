/*
 * qma7981.c - QMA7981 3轴加速度计 I2C 驱动实现
 *
 * 数据格式：14-bit 有符号整数，分布在 H 寄存器 [13:6] 和 L 寄存器 [5:2]
 * 转换公式：accel_mg = raw_14bit * (range_mg / 8192)
 *   例：±8G 量程 → 8000 mg / 8192 ≈ 0.977 mg/LSB
 */

#include "qma7981.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "QMA7981";

static qma7981_range_t s_range = QMA7981_RANGE_8G;

/* ---- I2C 底层读写 ---- */

static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(
        QMA7981_I2C_PORT, QMA7981_I2C_ADDR,
        &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(
        QMA7981_I2C_PORT, QMA7981_I2C_ADDR,
        buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_block(uint8_t reg_start, uint8_t *buf, size_t len)
{
    return i2c_master_write_read_device(
        QMA7981_I2C_PORT, QMA7981_I2C_ADDR,
        &reg_start, 1, buf, len, pdMS_TO_TICKS(100));
}

/* ---- 公开接口 ---- */

esp_err_t qma7981_init(qma7981_range_t range)
{
    s_range = range;
    esp_err_t ret;

    /* 1. 配置 I2C 主机 */
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = QMA7981_I2C_SDA_GPIO,
        .scl_io_num = QMA7981_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = QMA7981_I2C_FREQ_HZ,
    };
    ret = i2c_param_config(QMA7981_I2C_PORT, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config 失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_driver_install(QMA7981_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 验证芯片 ID */
    uint8_t chip_id = 0;
    ret = i2c_read_reg(QMA7981_REG_CHIP_ID, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取 Chip ID 失败: %s", esp_err_to_name(ret));
        return ret;
    }
    if (chip_id != 0xB3) {
        ESP_LOGE(TAG, "Chip ID 不匹配！期望 0xB3，实际 0x%02X", chip_id);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "✅ Chip ID 验证通过: 0x%02X", chip_id);

    /* 3. 软复位 */
    ret = i2c_write_reg(QMA7981_REG_RESET, 0xB6);  /* 软复位寄存器 */
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* 4. 设置量程 */
    ret = i2c_write_reg(QMA7981_REG_RANGE, (uint8_t)range);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置量程失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 5. 设置带宽 512Hz */
    ret = i2c_write_reg(QMA7981_REG_BW, 0x05);  /* BW=512Hz */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置带宽失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 6. 上电（使能传感器） */
    ret = i2c_write_reg(QMA7981_REG_PWR, 0x80);  /* PD=0 (active), signal_cond=1 */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "上电失败: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(50));  /* 等待传感器稳定 */
    ESP_LOGI(TAG, "✅ QMA7981 初始化完成（量程=0x%02X）", range);
    return ESP_OK;
}

esp_err_t qma7981_read(qma7981_data_t *data)
{
    if (data == NULL) return ESP_ERR_INVALID_ARG;

    /* 连续读取 6 字节：XL, XH, YL, YH, ZL, ZH */
    uint8_t buf[6];
    esp_err_t ret = i2c_read_block(QMA7981_REG_XOUT_L, buf, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取数据失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 解析 14-bit 有符号值 */
    /* 格式：H 寄存器 [bit7:0] = data[13:6], L 寄存器 [bit7:6] = data[5:4] (高2位) */
    /* 不同批次可能格式略有差异，此处采用最常见的 QMA7981 格式 */
    int16_t raw_x = (int16_t)(((uint16_t)buf[1] << 8) | buf[0]) >> 2;
    int16_t raw_y = (int16_t)(((uint16_t)buf[3] << 8) | buf[2]) >> 2;
    int16_t raw_z = (int16_t)(((uint16_t)buf[5] << 8) | buf[4]) >> 2;

    /* 符号扩展到 16-bit */
    if (raw_x & 0x2000) raw_x |= 0xC000;
    if (raw_y & 0x2000) raw_y |= 0xC000;
    if (raw_z & 0x2000) raw_z |= 0xC000;

    data->raw_x = raw_x;
    data->raw_y = raw_y;
    data->raw_z = raw_z;

    /* 转换为 mg（毫g）
     * 14-bit 范围: -8192 ~ +8191
     * 灵敏度 = range(mg) / 8192
     * ±2G  → 2000mg/8192 ≈ 0.244 mg/LSB
     * ±4G  → 4000mg/8192 ≈ 0.488 mg/LSB
     * ±8G  → 8000mg/8192 ≈ 0.977 mg/LSB
     * ±16G → 16000mg/8192 ≈ 1.953 mg/LSB
     * ±32G → 32000mg/8192 ≈ 3.906 mg/LSB
     */
    float range_mg;
    switch (s_range) {
        case QMA7981_RANGE_2G:  range_mg = 2000.0f;  break;
        case QMA7981_RANGE_4G:  range_mg = 4000.0f;  break;
        case QMA7981_RANGE_8G:  range_mg = 8000.0f;  break;
        case QMA7981_RANGE_16G: range_mg = 16000.0f; break;
        case QMA7981_RANGE_32G: range_mg = 32000.0f; break;
        default:                range_mg = 8000.0f;  break;
    }
    float scale = range_mg / 8192.0f;

    data->ax_mg = (float)raw_x * scale;
    data->ay_mg = (float)raw_y * scale;
    data->az_mg = (float)raw_z * scale;

    /* 读芯片 ID 供校验 */
    i2c_read_reg(QMA7981_REG_CHIP_ID, &data->chip_id);

    return ESP_OK;
}

void qma7981_deinit(void)
{
    i2c_driver_delete(QMA7981_I2C_PORT);
    ESP_LOGI(TAG, "QMA7981 资源已释放");
}