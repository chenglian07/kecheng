#ifndef QMA7981_H
#define QMA7981_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C 引脚配置（禁止修改） */
#define QMA7981_I2C_SDA        GPIO_NUM_4
#define QMA7981_I2C_SCL        GPIO_NUM_5
#define QMA7981_I2C_FREQ_HZ    400000
#define QMA7981_ADDR           0x12          /* 7-bit I2C 地址 */

/* 寄存器地址 */
#define QMA7981_REG_CHIP_ID    0x00          /* Chip ID                     */
#define QMA7981_REG_ACC_X_LSB  0x01          /* X 加速度 LSB                */
#define QMA7981_REG_ACC_X_MSB  0x02          /* X 加速度 MSB                */
#define QMA7981_REG_ACC_Y_LSB  0x03          /* Y 加速度 LSB                */
#define QMA7981_REG_ACC_Y_MSB  0x04          /* Y 加速度 MSB                */
#define QMA7981_REG_ACC_Z_LSB  0x05          /* Z 加速度 LSB                */
#define QMA7981_REG_ACC_Z_MSB  0x06          /* Z 加速度 MSB                */
#define QMA7981_REG_PWR_MGMT   0x11          /* 电源管理寄存器              */

/* 常用寄存器值 */
#define QMA7981_CHIP_ID_VAL    0x90          /* 实测量到的 Chip ID          */
#define QMA7981_PWR_ACTIVE     0xC0          /* Active 模式                 */
#define QMA7981_PWR_SLEEP      0x40          /* 休眠模式                    */

/* 满量程参数（默认 ±2g） */
#define QMA7981_RANGE_G        2.0f
#define QMA7981_MAX_14BIT      8191.0f

/* 加速数据结构体（单位：g） */
typedef struct {
    float ax;
    float ay;
    float az;
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;
} qma7981_accel_t;

/**
 * @brief 初始化 QMA7981（创建 I2C 总线 + 添加设备 + 设为 Active 模式）
 *
 * @return ESP_OK 成功
 */
esp_err_t qma7981_init(void);

/**
 * @brief 读取 Chip ID（WHO_AM_I）
 */
esp_err_t qma7981_get_chip_id(uint8_t *chip_id);

/**
 * @brief 读取三轴加速度值
 *
 * LSB 低 2 位为 NEW_DATA 状态位，函数内部静默掩码，
 * 不视为错误。
 */
esp_err_t qma7981_read_accel(qma7981_accel_t *accel);

/**
 * @brief I2C 总线扫描（打印所有有应答的设备地址）
 *
 * 基于 i2c_master_probe()，ESP-IDF v5.x 新 API 推荐做法。
 */
void qma7981_i2c_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* QMA7981_H */