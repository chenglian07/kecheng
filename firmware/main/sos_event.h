#ifndef SOS_EVENT_H
#define SOS_EVENT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Week 3: SOS 求助事件状态机
 *
 * 状态流转：
 *   idle → local_ack → uploading → delivered → acknowledged
 *                                   ↓
 *                                cancelled / failed / timeout
 *
 * 关键规则：
 *   - 断网时仍可进入 local_ack（本地确认不依赖网络）
 *   - 恢复网络后自动重试上传，保留原始 local_ack 时间
 *   - 上传失败不得显示 delivered
 *   - cancelled 终态不可被 acknowledged 覆盖
 */

/* 求助事件状态 */
typedef enum {
    SOS_STATE_IDLE = 0,
    SOS_STATE_LOCAL_ACK,
    SOS_STATE_UPLOADING,
    SOS_STATE_DELIVERED,
    SOS_STATE_ACKNOWLEDGED,
    SOS_STATE_CANCELLED,
    SOS_STATE_FAILED,
    SOS_STATE_TIMEOUT,
} sos_state_t;

/* 求助事件信息 */
typedef struct {
    char event_id[48];           /* 事件 ID（板端生成） */
    sos_state_t state;           /* 当前状态 */
    double ts_created;           /* 创建时间（板端秒，自启动以来） */
    double ts_local_ack;         /* 本地确认时间（板端秒） */
    double ts_server_received;   /* 服务端接收时间（-1 = 未收到） */
    double ts_viewer_ack;        /* 查看者回应时间（-1 = 未回应） */
    double ts_cancelled;         /* 取消时间（-1 = 未取消） */
    char source[16];             /* 触发来源: "button" / "test" */
    int upload_retry_count;      /* 上传重试次数 */
} sos_event_t;

/* 上传超时（毫秒） */
#define SOS_UPLOAD_TIMEOUT_MS     30000   /* 单次上传超时 30 秒 */
#define SOS_UPLOAD_MAX_RETRY      3       /* 最大重试次数 */
#define SOS_RESPONSE_POLL_MS      3000    /* 轮询回应间隔 3 秒 */

/**
 * @brief 初始化 SOS 事件模块，启动后台任务
 * @return ESP_OK 成功
 */
esp_err_t sos_event_init(void);

/**
 * @brief 触发一次求助事件（按键回调中调用）
 *
 * 若当前状态为 idle 或终态（cancelled/failed/timeout），
 * 则创建新事件并进入 local_ack。
 * 若当前已有活跃事件，则忽略本次按键。
 */
void sos_event_trigger(void);

/**
 * @brief 处理服务端回应（cmd_poll 中调用）
 * @param event_id 事件 ID
 * @param viewer_ack_at 回应时间（ISO 8601 字符串，可为 NULL）
 */
void sos_event_handle_response(const char *event_id, const char *viewer_ack_at);

/**
 * @brief 处理取消命令（cmd_poll 中调用）
 * @param event_id 事件 ID
 * @param cancelled_at 取消时间（ISO 8601 字符串，可为 NULL）
 */
void sos_event_handle_cancel(const char *event_id, const char *cancelled_at);

/**
 * @brief 获取当前事件信息（线程安全）
 * @param out 输出事件信息
 * @return true 有活跃事件，false 无事件
 */
bool sos_event_get_current(sos_event_t *out);

/**
 * @brief 获取状态名称字符串
 */
const char *sos_state_str(sos_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* SOS_EVENT_H */