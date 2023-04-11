#ifndef ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_LED_H_
#define ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_LED_H_

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"  // NOTE: #include "FreeRTOS.h" must appear before #include "event_groups.h"

#include "esp_err.h"
#include "esp_bit_defs.h"

esp_err_t led_init(void);

extern EventGroupHandle_t led_event_group;

typedef enum {
    LED_EVENT_ON    = BIT0,
    LED_EVENT_OFF   = BIT1,
    LED_EVENT_BLINK = BIT2,
} led_event_t;

#endif  // ESP32_WIFI_ONEWIRE_MQTT_MAIN_INCLUDE_LED_H_
