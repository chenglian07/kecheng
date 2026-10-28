/*
 * qma7981.c - QMA7981 3轴加速度计 I2C 驱动实现
 *
 * 初始化逻辑：
 *   - 只写 PWR 寄存器 (0x11 = 0xC0) 激活传感器
 *   - 不写 RANGE / BW 寄存器（使用默认 ±2g）
 *   - 不做软复位
 *
 * 数据格式：14-bit 有符号整数，小端序
 *   LSB [7:2] = 数据低 6 位，[1:0] = NEW_DATA 状态位（掩码 & 0xFC）
 *   MSB [7:0] = 数据高 8 位
 *
 * 原始值提取：raw_14bit = ((MSB << 8) | (LSB & 0xFC))
 *   → 14-bit 有符号范围 [-8192, +8191]
 *   → 代码内 /4 得到 12-bit 有符号范围 [-2048, +2047]
 *
 * 加速度换算（±2g 默认量程）：
 *   14-bit 灵敏度 ≈ 16384 LSB/g（8192 LSB/g 更常用）
 *   accel_mg = raw_12bit * 1000.0 / 2048.0
 *            = raw_14bit * 2000.0 / 16384.0
 */

#include "qma7981.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "QMA7981";

static bool s_sensor_active = false;
static uint8_t s_chip_id = 0;

static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;

static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *val)
{
    if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1,
                                       val, 1, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_write_reg(uint8_t reg, uint8_t val)
{
    if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev_handle, buf, sizeof(buf),
                                pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_block(uint8_t reg_start, uint8_t *buf, size_t len)
{
    if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev_handle, &reg_start, 1,
                                       buf, len, pdMS_TO_TICKS(100));
}

esp_err_t qma7981_init(qma7981_range_t range)
{
    (void)range;  /* 忽略量程参数，使用默认 2g（与工作版本一致） */
    s_sensor_active = false;
    esp_err_t ret;

    ESP_LOGI(TAG, "I2C init (SDA=GPIO%d, SCL=GPIO%d, %d Hz)",
             QMA7981_I2C_SDA_GPIO, QMA7981_I2C_SCL_GPIO, QMA7981_I2C_FREQ_HZ);

    /* 1. 创建 I2C 主总线 */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = QMA7981_I2C_SDA_GPIO,
        .scl_io_num = QMA7981_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 添加 QMA7981 设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMA7981_I2C_ADDR,
        .scl_speed_hz = QMA7981_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. 设置 Active 模式（唤醒传感器）— 与工作版本完全一致 */
    ret = i2c_write_reg(QMA7981_REG_PWR, 0xC0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write PWR failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 4. 读取 Chip ID */
    ret = i2c_read_reg(QMA7981_REG_CHIP_ID, &s_chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取 Chip ID 失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Chip ID: 0x%02X", s_chip_id);
    if (s_chip_id == QMA7981_CHIP_ID_EXPECT) {
        ESP_LOGI(TAG, "  ✅ Chip ID 匹配数据手册 (0x%02X)", QMA7981_CHIP_ID_EXPECT);
    } else if (s_chip_id == QMA7981_CHIP_ID_ALT) {
        ESP_LOGI(TAG, "  ✅ Chip ID = 0x%02X (ESP32-S3-EYE 实测值，正常)", QMA7981_CHIP_ID_ALT);
    } else if (s_chip_id == 0x00 || s_chip_id == 0xFF) {
        ESP_LOGW(TAG, "  ⚠️ Chip ID 异常 (0x%02X)，I2C 总线可能有故障", s_chip_id);
    } else {
        ESP_LOGW(TAG, "  ⚠️ Chip ID = 0x%02X (非预期值，但继续运行)", s_chip_id);
    }

    /* 5. Dump 关键寄存器用于调试 */
    uint8_t reg_val;
    for (uint8_t r = 0x0F; r <= 0x11; r++) {
        if (i2c_read_reg(r, &reg_val) == ESP_OK) {
            ESP_LOGI(TAG, "  REG[0x%02X] = 0x%02X", r, reg_val);
        }
    }

    /* 6. 标记为激活 */
    s_sensor_active = true;
    ESP_LOGI(TAG, "✅ 传感器已激活 (PWR=0xC0, 默认 2g)");

    /* 7. 延时后读取一次数据验证 */
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t test_buf[6] = {0};
    ret = i2c_read_block(QMA7981_REG_XOUT_L, test_buf, 6);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "验证原始字节: [%02X %02X] [%02X %02X] [%02X %02X]",
                 test_buf[0], test_buf[1], test_buf[2],
                 test_buf[3], test_buf[4], test_buf[5]);
        int16_t tx = (int16_t)((test_buf[0] & 0xFC) | ((uint16_t)test_buf[1] << 8)) / 4;
        int16_t ty = (int16_t)((test_buf[2] & 0xFC) | ((uint16_t)test_buf[3] << 8)) / 4;
        int16_t tz = (int16_t)((test_buf[4] & 0xFC) | ((uint16_t)test_buf[5] << 8)) / 4;
        float tx_mg = (float)tx * 1000.0f / 2048.0f;
        float ty_mg = (float)ty * 1000.0f / 2048.0f;
        float tz_mg = (float)tz * 1000.0f / 2048.0f;
        ESP_LOGI(TAG, "验证读取: raw_x=%d (%.1f mg), raw_y=%d (%.1f mg), raw_z=%d (%.1f mg)",
                 tx, tx_mg, ty, ty_mg, tz, tz_mg);
    }

    return ESP_OK;
}

esp_err_t qma7981_read(qma7981_data_t *data)
{
    if (data == NULL) return ESP_ERR_INVALID_ARG;
    data->chip_id = s_chip_id;

    if (!s_sensor_active) {
        data->raw_x = data->raw_y = data->raw_z = 0;
        data->ax_mg = data->ay_mg = data->az_mg = 0.0f;
        return ESP_OK;
    }

    uint8_t buf[6];
    esp_err_t ret = i2c_read_block(QMA7981_REG_XOUT_L, buf, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取数据失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 提取 12-bit 有符号原始值：
     * 14-bit = ((LSB & 0xFC) | (MSB << 8))，再 /4 去掉低 2 位状态位
     * 结果范围 [-2048, +2047]，对应 ±2g */
    int16_t raw_x = (int16_t)((buf[0] & 0xFC) | ((uint16_t)buf[1] << 8)) / 4;
    int16_t raw_y = (int16_t)((buf[2] & 0xFC) | ((uint16_t)buf[3] << 8)) / 4;
    int16_t raw_z = (int16_t)((buf[4] & 0xFC) | ((uint16_t)buf[5] << 8)) / 4;

    data->raw_x = raw_x;
    data->raw_y = raw_y;
    data->raw_z = raw_z;

    /* ±2g 量程，12-bit 有符号原始值 → mg
     * 12-bit 满量程 ±2048 对应 ±2g = ±2000mg
     * 灵敏度 = 2048 LSB / 2g → accel_mg = raw * 1000 / 2048
     * （等价于 14-bit 公式: raw_14bit * 2000.0 / 16384.0）
     */
    data->ax_mg = (float)raw_x * 1000.0f / 2048.0f;
    data->ay_mg = (float)raw_y * 1000.0f / 2048.0f;
    data->az_mg = (float)raw_z * 1000.0f / 2048.0f;

    return ESP_OK;
}

bool qma7981_is_active(void)
{
    return s_sensor_active;
}

void qma7981_deinit(void)
{
    if (s_dev_handle) {
        i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
    }
    if (s_bus_handle) {
        i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
    }
    ESP_LOGI(TAG, "QMA7981 资源已释放");
}