/**
 * Week 3: SOS 求助按键模块
 *
 * 硬件：板载 BOOT 键 GPIO0，低电平按下，内部上拉
 * 消抖：软件 50ms
 *
 * 降级说明：
 *   若需外接按键，修改 sos_button.h 中的 GPIO 和 ACTIVE_LOW 定义即可。
 */

#include "sos_button.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "sos_btn";

static sos_button_cb_t s_callback = NULL;
static volatile bool s_pressed = false;

/* 消抖状态 */
static int64_t s_last_edge_us = 0;
static bool s_last_raw = false;      /* 上一次读取的原始电平（true=按下） */
static bool s_debounced = false;     /* 消抖后的稳定状态 */

static bool read_button_raw(void)
{
    int level = gpio_get_level(SOS_BUTTON_GPIO);
    if (SOS_BUTTON_ACTIVE_LOW) {
        return (level == 0);   /* 低电平 = 按下 */
    } else {
        return (level == 1);   /* 高电平 = 按下 */
    }
}

static void button_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "button monitor task started (GPIO%d, active_low=%d, debounce=%dms)",
             SOS_BUTTON_GPIO, SOS_BUTTON_ACTIVE_LOW, SOS_DEBOUNCE_MS);

    while (1) {
        bool raw = read_button_raw();
        int64_t now_us = esp_timer_get_time();

        if (raw != s_last_raw) {
            /* 电平变化，开始消抖计时 */
            s_last_raw = raw;
            s_last_edge_us = now_us;
        } else if (raw != s_debounced) {
            /* 电平与消抖状态不同，检查是否已过消抖时间 */
            int64_t elapsed_us = now_us - s_last_edge_us;
            if (elapsed_us >= (int64_t)SOS_DEBOUNCE_MS * 1000) {
                s_debounced = raw;
                s_pressed = raw;

                if (raw && s_callback) {
                    /* 按下边沿触发回调 */
                    ESP_LOGI(TAG, "BUTTON PRESSED (debounced)");
                    s_callback(true);
                } else if (!raw) {
                    ESP_LOGD(TAG, "BUTTON RELEASED (debounced)");
                }
            }
        }

        /* 每 10ms 采样一次 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}

esp_err_t sos_button_init(sos_button_cb_t cb)
{
    s_callback = cb;

    /* 配置 GPIO：输入模式，内部上拉（BOOT 键通常已有外部上拉） */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SOS_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SOS button initialized: GPIO%d, active_low=%d",
             SOS_BUTTON_GPIO, SOS_BUTTON_ACTIVE_LOW);

    /* 启动按键检测任务 */
    BaseType_t xret = xTaskCreate(
        button_task,
        "sos_btn",
        2048,       /* 栈大小 */
        NULL,
        3,          /* 中优先级（高于 cmd_poll 的 1） */
        NULL
    );

    if (xret != pdPASS) {
        ESP_LOGE(TAG, "failed to create button task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool sos_button_is_pressed(void)
{
    return s_pressed;
}