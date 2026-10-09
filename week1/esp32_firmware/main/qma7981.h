/*
 * qma7981.h - QMA7981 3轴加速度计 I2C 驱动
 *
 * 硬件连接：
 *   SDA = GPIO4, SCL = GPIO5
 *   I2C 地址 = 0x12
 *
 * 注意：QMA7981 是纯加速度计，不含陀螺仪。
 *       输出 ax/ay/az 三轴加速度，单位 mg（毫g）。
 */

#ifndef QMA7981_H
#define QMA7981_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C 引脚定义 */
#define QMA7981_I2C_SDA_GPIO    4
#define QMA7981_I2C_SCL_GPIO    5
#define QMA7981_I2C_ADDR        0x12
#define QMA7981_I2C_PORT        I2C_NUM_0
#define QMA7981_I2C_FREQ_HZ    400000   /* 400kHz Fast Mode */

/* 寄存器地址 */
#define QMA7981_REG_CHIP_ID     0x00    /* 期望值 0xB3 */
#define QMA7981_REG_XOUT_L      0x01
#define QMA7981_REG_XOUT_H      0x02
#define QMA7981_REG_YOUT_L      0x03
#define QMA7981_REG_YOUT_H      0x04
#define QMA7981_REG_ZOUT_L      0x05
#define QMA7981_REG_ZOUT_H      0x06
#define QMA7981_REG_STEP_L      0x07
#define QMA7981_REG_STEP_H      0x08
#define QMA7981_REG_INT_ST0     0x09
#define QMA7981_REG_INT_ST1     0x0A
#define QMA7981_REG_INT_ST2     0x0B
#define QMA7981_REG_RANGE       0x0F    /* 量程选择 */
#define QMA7981_REG_BW          0x10    /* 带宽选择 */
#define QMA7981_REG_PWR         0x11    /* 电源控制 */
#define QMA7981_REG_INT_EN0     0x13
#define QMA7981_REG_INT_EN1     0x14
#define QMA7981_REG_INT_EN2     0x15
#define QMA7981_REG_INT_MAP0    0x16
#define QMA7981_REG_INT_MAP1    0x17
#define QMA7981_REG_INT_MAP2    0x18
#define QMA7981_REG_RESET       0x36    /* 软复位 */

/* 量程定义 (REG 0x0F) */
typedef enum {
    QMA7981_RANGE_2G  = 0x01,
    QMA7981_RANGE_4G  = 0x02,
    QMA7981_RANGE_8G  = 0x04,
    QMA7981_RANGE_16G = 0x08,
    QMA7981_RANGE_32G = 0x0F,
} qma7981_range_t;

/* 加速度数据结构 */
typedef struct {
    int16_t raw_x;      /* 原始值 */
    int16_t raw_y;
    int16_t raw_z;
    float   ax_mg;      /* 加速度，单位 mg */
    float   ay_mg;
    float   az_mg;
    uint8_t chip_id;    /* 芯片 ID */
} qma7981_data_t;

/**
 * @brief  初始化 QMA7981 传感器
 *         配置 I2C 主机、验证芯片 ID、设置量程、启动传感器
 * @param  range  量程选择（推荐 QMA7981_RANGE_8G）
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t qma7981_init(qma7981_range_t range);

/**
 * @brief  读取一次三轴加速度数据
 * @param  data  输出数据指针
 * @return ESP_OK 成功
 */
esp_err_t qma7981_read(qma7981_data_t *data);

/**
 * @brief  反初始化，释放 I2C 资源
 */
void qma7981_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* QMA7981_H */