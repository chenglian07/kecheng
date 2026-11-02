/**
 * Week 3: SOS 求助本地反馈模块
 *
 * 降级方案：无物理 LED/蜂鸣器时，以串口日志作为本地确认证据。
 * 日志格式严格遵守：[SOS] <状态> event_id=<id> [附加信息]
 */

#include "sos_feedback.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sos_fb";

/* 是否有物理 LED */
static bool s_has_led = false;
/* 是否有物理蜂鸣器 */
static bool s_has_buzzer = false;

/* ----- 内部辅助 ----- */

static void led_on(void)
{
    if (!s_has_led) return;
    gpio_set_level(SOS_LED_GPIO, SOS_LED_ACTIVE_HIGH ? 1 : 0);
}

static void led_off(void)
{
    if (!s_has_led) return;
    gpio_set_level(SOS_LED_GPIO, SOS_LED_ACTIVE_HIGH ? 0 : 1);
}

static void led_blink(int times, int on_ms, int off_ms)
{
    if (!s_has_led) return;
    for (int i = 0; i < times; i++) {
        led_on();
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

static void buzzer_beep(int duration_ms)
{
    if (!s_has_buzzer) return;
    gpio_set_level(SOS_BUZZER_GPIO, SOS_BUZZER_ACTIVE_HIGH ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(SOS_BUZZER_GPIO, SOS_BUZZER_ACTIVE_HIGH ? 0 : 1);
}

static const char *state_to_str(sos_fb_state_t state)
{
    switch (state) {
        case SOS_FB_IDLE:         return "idle";
        case SOS_FB_LOCAL_ACK:    return "local_ack";
        case SOS_FB_UPLOADING:    return "uploading";
        case SOS_FB_DELIVERED:    return "delivered";
        case SOS_FB_ACKNOWLEDGED: return "acknowledged";
        case SOS_FB_CANCELLED:    return "cancelled";
        case SOS_FB_FAILED:       return "failed";
        case SOS_FB_TIMEOUT:      return "timeout";
        default:                  return "unknown";
    }
}

/* ----- 公共接口 ----- */

esp_err_t sos_feedback_init(void)
{
    /* LED 初始化 */
    if (SOS_LED_GPIO != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << SOS_LED_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret == ESP_OK) {
            s_has_led = true;
            led_off();
            ESP_LOGI(TAG, "LED initialized: GPIO%d (active_high=%d)",
                     SOS_LED_GPIO, SOS_LED_ACTIVE_HIGH);
        } else {
            ESP_LOGW(TAG, "LED GPIO config failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "NO physical LED configured (GPIO_NUM_NC). "
                 "Local feedback will be serial log only.");
    }

    /* 蜂鸣器初始化 */
    if (SOS_BUZZER_GPIO != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << SOS_BUZZER_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret == ESP_OK) {
            s_has_buzzer = true;
            gpio_set_level(SOS_BUZZER_GPIO, SOS_BUZZER_ACTIVE_HIGH ? 0 : 1);
            ESP_LOGI(TAG, "Buzzer initialized: GPIO%d (active_high=%d)",
                     SOS_BUZZER_GPIO, SOS_BUZZER_ACTIVE_HIGH);
        } else {
            ESP_LOGW(TAG, "Buzzer GPIO config failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "NO physical buzzer configured (GPIO_NUM_NC). "
                 "Buzzer feedback disabled.");
    }

    return ESP_OK;
}

void sos_feedback_update(sos_fb_state_t state, const char *event_id, const char *extra)
{
    const char *eid = event_id ? event_id : "none";
    const char *ext = extra ? extra : "";

    /* 串口日志始终输出，作为证据 */
    switch (state) {
        case SOS_FB_IDLE:
            ESP_LOGI(TAG, "[SOS] idle event_id=%s %s", eid, ext);
            led_off();
            break;

        case SOS_FB_LOCAL_ACK:
            /* local_ack: LED 快闪 3 次 + 蜂鸣器短鸣 */
            ESP_LOGI(TAG, "[SOS] local_ack event_id=%s source=button %s", eid, ext);
            ESP_LOGW(TAG, ">>> [SOS] 本地已确认按键触发 <<< "
                     "(若无物理LED/蜂鸣器，此日志即为本地确认证据)");
            led_blink(3, 100, 100);
            buzzer_beep(200);
            break;

        case SOS_FB_UPLOADING:
            ESP_LOGI(TAG, "[SOS] uploading event_id=%s %s", eid, ext);
            /* uploading: LED 慢闪 */
            led_blink(1, 500, 500);
            break;

        case SOS_FB_DELIVERED:
            ESP_LOGI(TAG, "[SOS] delivered event_id=%s %s", eid, ext);
            /* delivered: LED 常亮 */
            led_on();
            buzzer_beep(100);
            break;

        case SOS_FB_ACKNOWLEDGED:
            ESP_LOGI(TAG, "[SOS] acknowledged event_id=%s %s", eid, ext);
            /* acknowledged: LED 保持亮 + 蜂鸣器两声 */
            led_on();
            buzzer_beep(150);
            vTaskDelay(pdMS_TO_TICKS(100));
            buzzer_beep(150);
            break;

        case SOS_FB_CANCELLED:
            ESP_LOGI(TAG, "[SOS] cancelled event_id=%s %s", eid, ext);
            /* cancelled: LED 灭 + 蜂鸣器一声低鸣 */
            led_off();
            buzzer_beep(300);
            break;

        case SOS_FB_FAILED:
            ESP_LOGW(TAG, "[SOS] failed event_id=%s %s", eid, ext);
            /* failed: LED 红色闪烁（若有） */
            led_blink(5, 200, 200);
            break;

        case SOS_FB_TIMEOUT:
            ESP_LOGW(TAG, "[SOS] timeout event_id=%s %s", eid, ext);
            led_off();
            break;

        default:
            ESP_LOGW(TAG, "[SOS] unknown state=%d event_id=%s", state, eid);
            break;
    }

    /* 打印硬件状态总结 */
    ESP_LOGI(TAG, "[SOS] feedback state=%s has_led=%d has_buzzer=%d",
             state_to_str(state), s_has_led, s_has_buzzer);
}