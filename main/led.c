#include "led.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "types.h"

static const char *TAG = "led";

#define LED_1 GPIO_NUM_2

typedef enum {
    LED_OFF = 0,
    LED_ON  = 1,
} led_state_t;

EventGroupHandle_t led_event_group = NULL;
static esp_timer_handle_t led_blink_timer = NULL;

static void led_blink_timer_callback(void *arg);

static void led_blink_timer_start(uint64_t timeout_us)
{
    if (led_blink_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = led_blink_timer_callback,
            .arg = NULL,
            .name = "Led timer"
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &led_blink_timer));
    }
    ESP_ERROR_CHECK(esp_timer_start_once(led_blink_timer, timeout_us));
}

static void led_blink_timer_stop(void)
{
    if (led_blink_timer != NULL) {
        if (esp_timer_is_active(led_blink_timer)) {
            ESP_ERROR_CHECK(esp_timer_stop(led_blink_timer));
        }
        ESP_ERROR_CHECK(esp_timer_delete(led_blink_timer));
        led_blink_timer = NULL;
    }
}

static void led_blink_timer_callback(void *arg)
{
    static bool is_led_on = true;
    is_led_on = !is_led_on;

    if (is_led_on) {
        gpio_set_level(LED_1, LED_OFF);
        led_blink_timer_start(9 * 100000);  // 900 ms
    } else {
        gpio_set_level(LED_1, LED_ON);
        led_blink_timer_start(1 * 100000);  // 100 ms
    }
}

static void led_task(void *params)
{
    while (true) {
        EventBits_t event_bits = xEventGroupWaitBits(led_event_group, LED_EVENT_ON | LED_EVENT_OFF | LED_EVENT_BLINK,
                                         pdTRUE, pdFALSE, portMAX_DELAY);

        if (event_bits & LED_EVENT_ON) {
            led_blink_timer_stop();
            gpio_set_level(LED_1, LED_ON);
        } else if (event_bits & LED_EVENT_OFF) {
            led_blink_timer_stop();
            gpio_set_level(LED_1, LED_OFF);
        } else if (event_bits & LED_EVENT_BLINK) {
            led_blink_timer_start(0);
        }
    }
}

esp_err_t led_init(void)
{
    const gpio_config_t io_config = {
    	.pin_bit_mask = (1ULL << LED_1),  // ((1ULL << LED_1) | (1ULL << LED_2) ...)
    	.mode = GPIO_MODE_OUTPUT,
    	.pull_up_en = GPIO_PULLUP_DISABLE,
    	.pull_down_en = GPIO_PULLDOWN_DISABLE,
    	.intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_config);
    gpio_set_level(LED_1, LED_OFF);

    led_event_group = xEventGroupCreate();
    if (led_event_group == NULL) {
        ESP_LOGE(TAG, "led_event_group: Event Group was not created. Could not allocate required memory");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t status = xTaskCreate(led_task, "led_task", configMINIMAL_STACK_SIZE * 4, NULL, PRIORITY_MIDDLE, NULL);
    if (status != pdPASS) {
        ESP_LOGE(TAG, "led_task(): Task was not created. Could not allocate required memory");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "led_init() finished successfully");
    return ESP_OK;
}
