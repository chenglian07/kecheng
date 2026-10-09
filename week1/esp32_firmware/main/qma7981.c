/*
 * qma7981.c - QMA7981 3轴加速度计 I2C 驱动实现
 *
 * 数据格式：14-bit 有符号整数，分布在 MSB [13:6] 和 LSB [5:0]
 * LSB 低2位为状态位，需掩码 (& 0xFC)
 * 转换公式：accel_mg = raw_14bit * (range_mg / 8192)
 *
 * 注意：初始化时不做软复位(0xB6→0x36)，直接写 PWR 寄存器激活传感器。
 *       软复位会导致低地址寄存器(0x00-0x11)锁定为只读状态。
 */

#include "qma7981.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "QMA7981";

static qma7981_range_t s_range = QMA7981_RANGE_8G;
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

/* 根据量程枚举获取满量程 mg 值 */
static float range_to_mg(qma7981_range_t range)
{
    switch (range) {
        case QMA7981_RANGE_2G:  return 2000.0f;
        case QMA7981_RANGE_4G:  return 4000.0f;
        case QMA7981_RANGE_8G:  return 8000.0f;
        case QMA7981_RANGE_16G: return 16000.0f;
        case QMA7981_RANGE_32G: return 32000.0f;
        default:                return 2000.0f;
    }
}

esp_err_t qma7981_init(qma7981_range_t range)
{
    s_range = range;
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

    /* 3. 直接激活传感器（不做软复位！软复位会导致寄存器锁定） */
    ret = i2c_write_reg(QMA7981_REG_PWR, 0xC0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write PWR failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 4. 验证 Chip ID */
    ret = i2c_read_reg(QMA7981_REG_CHIP_ID, &s_chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取 Chip ID 失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Chip ID: 0x%02X", s_chip_id);

    if (s_chip_id != 0xB3 && s_chip_id != 0x90) {
        ESP_LOGW(TAG, "Chip ID 非典型值: 0x%02X (期望 0xB3/0x90)", s_chip_id);
    }

    /* 5. 设置量程（在 PWR 写入之后） */
    ret = i2c_write_reg(QMA7981_REG_RANGE, (uint8_t)range);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "设置量程失败: %s (继续运行)", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 6. 标记为激活（PWR write 无 I2C 错误即认为成功） */
    s_sensor_active = true;
    ESP_LOGI(TAG, "✅ 传感器已激活 (PWR=0xC0, RANGE=0x%02X)", (uint8_t)range);

    /* 7. 尝试读取一次数据来验证 */
    uint8_t test_buf[6] = {0};
    ret = i2c_read_block(QMA7981_REG_XOUT_L, test_buf, 6);
    if (ret == ESP_OK) {
        int16_t tx = (int16_t)((test_buf[0] & 0xFC) | ((uint16_t)test_buf[1] << 8)) / 4;
        int16_t ty = (int16_t)((test_buf[2] & 0xFC) | ((uint16_t)test_buf[3] << 8)) / 4;
        int16_t tz = (int16_t)((test_buf[4] & 0xFC) | ((uint16_t)test_buf[5] << 8)) / 4;
        ESP_LOGI(TAG, "验证读取: raw_x=%d, raw_y=%d, raw_z=%d", tx, ty, tz);
        if (tx == 0 && ty == 0 && tz == 0) {
            ESP_LOGW(TAG, "⚠️ 首次读取全零，可能需要等待传感器稳定");
        }
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

    int16_t raw_x = (int16_t)((buf[0] & 0xFC) | ((uint16_t)buf[1] << 8)) / 4;
    int16_t raw_y = (int16_t)((buf[2] & 0xFC) | ((uint16_t)buf[3] << 8)) / 4;
    int16_t raw_z = (int16_t)((buf[4] & 0xFC) | ((uint16_t)buf[5] << 8)) / 4;

    data->raw_x = raw_x;
    data->raw_y = raw_y;
    data->raw_z = raw_z;

    float full_scale_mg = range_to_mg(s_range);
    data->ax_mg = (float)raw_x * full_scale_mg / 8192.0f;
    data->ay_mg = (float)raw_y * full_scale_mg / 8192.0f;
    data->az_mg = (float)raw_z * full_scale_mg / 8192.0f;

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