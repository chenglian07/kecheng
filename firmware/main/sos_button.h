#ifndef SOS_BUTTON_H
#define SOS_BUTTON_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Week 3: SOS 求助按键模块
 *
 * 硬件配置（降级方案）：
 *   - 按键：板载 BOOT 键 GPIO0，低电平按下，内部上拉
 *   - 消抖：软件 50ms
 *
 * 若无外接按键，GPIO0 (BOOT) 始终可用。
 */

/* 按键引脚配置 */
#define SOS_BUTTON_GPIO       GPIO_NUM_0    /* BOOT 键 */
#define SOS_BUTTON_ACTIVE_LOW true          /* 低电平按下 */
#define SOS_DEBOUNCE_MS       50            /* 消抖时间 */

/**
 * @brief 按键事件回调函数类型
 * @param pressed true=按下, false=释放
 */
typedef void (*sos_button_cb_t)(bool pressed);

/**
 * @brief 初始化按键 GPIO 并启动检测任务
 * @param cb 按键按下回调（仅按下边沿触发，释放不回调）
 * @return ESP_OK 成功
 */
esp_err_t sos_button_init(sos_button_cb_t cb);

/**
 * @brief 查询按键当前是否按下（已消抖）
 */
bool sos_button_is_pressed(void);

#ifdef __cplusplus
}
#endif

#endif /* SOS_BUTTON_H */