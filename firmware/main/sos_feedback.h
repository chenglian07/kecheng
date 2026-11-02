#ifndef SOS_FEEDBACK_H
#define SOS_FEEDBACK_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Week 3: SOS 求助本地反馈模块
 *
 * 硬件降级方案：
 *   - LED：可配置 GPIO，默认 GPIO_NUM_NC（-1）= 无物理 LED
 *   - 蜂鸣器：可配置 GPIO，默认 GPIO_NUM_NC（-1）= 无蜂鸣器
 *   - 无物理输出时，以串口日志作为"本地确认"证据
 *
 * 文档与 Web 中必须标注：
 *   "本次运行无物理 LED/蜂鸣器，本地反馈以串口日志为证据"
 *   物理反馈闭环中"实体反馈输出"标记为 **待补验**
 */

/* ===== 引脚配置（根据实际硬件修改） ===== */

/* LED GPIO：
 *   ESP32-S3-EYE 板载无用户可控通用 LED
 *   若有外接 LED，改为实际 GPIO 号（如 GPIO7）
 *   GPIO_NUM_NC = -1 表示无物理 LED
 */
#define SOS_LED_GPIO            GPIO_NUM_NC

/* LED 有效电平：true = 高电平亮 */
#define SOS_LED_ACTIVE_HIGH     true

/* 蜂鸣器 GPIO：
 *   默认关闭（GPIO_NUM_NC = -1）
 *   若有有源蜂鸣器，改为实际 GPIO 号（如 GPIO8）
 */
#define SOS_BUZZER_GPIO         GPIO_NUM_NC

/* 蜂鸣器有效电平：true = 高电平响 */
#define SOS_BUZZER_ACTIVE_HIGH  true

/**
 * @brief 求助事件状态枚举（与 sos_event.h 保持一致）
 */
typedef enum {
    SOS_FB_IDLE = 0,
    SOS_FB_LOCAL_ACK,
    SOS_FB_UPLOADING,
    SOS_FB_DELIVERED,
    SOS_FB_ACKNOWLEDGED,
    SOS_FB_CANCELLED,
    SOS_FB_FAILED,
    SOS_FB_TIMEOUT,
} sos_fb_state_t;

/**
 * @brief 初始化反馈硬件（LED/蜂鸣器 GPIO）
 * @return ESP_OK 成功
 */
esp_err_t sos_feedback_init(void);

/**
 * @brief 根据状态输出对应的本地反馈
 *
 * 有物理 LED/蜂鸣器时驱动硬件；无硬件时输出明确的串口日志。
 * 日志格式：[SOS] <状态> event_id=<id> [附加信息]
 *
 * @param state 当前求助事件状态
 * @param event_id 事件 ID（可为 NULL）
 * @param extra 附加信息字符串（可为 NULL）
 */
void sos_feedback_update(sos_fb_state_t state, const char *event_id, const char *extra);

#ifdef __cplusplus
}
#endif

#endif /* SOS_FEEDBACK_H */