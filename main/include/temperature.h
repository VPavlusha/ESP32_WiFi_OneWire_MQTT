#ifndef ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_TEMPERATURE_H_
#define ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_TEMPERATURE_H_

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"  // NOTE: #include "FreeRTOS.h" must appear in source files before #include "queue.h"

#include "esp_err.h"

typedef struct {
    uint8_t device;
    float temperature;
} temperature_device_t;

esp_err_t ds18b20_init(void);

extern QueueHandle_t temperature_queue;

#endif  // ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_TEMPERATURE_H_
