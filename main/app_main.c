#include "esp_check.h"

#include "led.h"
#include "mqtt.h"
#include "non_volatile_storage.h"
#include "task_monitor.h"
#include "temperature.h"
#include "wifi.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());

    ESP_ERROR_CHECK(led_init());
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(ds18b20_init());
    ESP_ERROR_CHECK(mqtt_init());

    // NOTE: USE_TRACE_FACILITY and GENERATE_RUN_TIME_STATS must be defined as 1
    // in FreeRTOSConfig.h for this API function task_monitor() to be available.
    ESP_ERROR_CHECK(task_monitor());
}
