#ifndef ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_TASK_MONITOR_H_
#define ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_TASK_MONITOR_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: USE_TRACE_FACILITY and GENERATE_RUN_TIME_STATS must be defined as 1
// in FreeRTOSConfig.h for this API function task_monitor() to be available.

/**
 * @brief Create a new task to monitor the status and activity of other FreeRTOS tasks in the system
 *
 * @return
 *         - ESP_OK           Success.
 *         - ESP_ERR_NO_MEM   Out of memory.
 */
esp_err_t task_monitor(void);

#ifdef __cplusplus
}
#endif

#endif  // ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_TASK_MONITOR_H_
