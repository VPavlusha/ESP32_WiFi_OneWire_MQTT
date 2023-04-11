#include "temperature.h"

#include <math.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"

#include "onewire_bus.h"
#include "ds18b20.h"

#include "types.h"

static const char *TAG = "temperature";

typedef struct {
    uint8_t device_num;
    onewire_bus_handle_t handle;
    uint8_t device_rom_id[CONFIG_ONEWIRE_NUMBER_OF_DEVICES][8];
} ds18b20_task_params_t;

QueueHandle_t temperature_queue = NULL;

#define AVERAGE_ARRAY_SIZE 3
static float get_average_temperature(size_t device, float temperature)
{
    static float average_array[CONFIG_ONEWIRE_NUMBER_OF_DEVICES][AVERAGE_ARRAY_SIZE];
    static bool is_average_array_init_with_nan = false;

    if (!is_average_array_init_with_nan) {
        for (size_t i = 0; i < CONFIG_ONEWIRE_NUMBER_OF_DEVICES; ++i) {
            for (size_t k = 0; k < AVERAGE_ARRAY_SIZE; ++k) {
                average_array[i][k] = NAN;
            }
        }
        is_average_array_init_with_nan = true;
    }

    static size_t index[CONFIG_ONEWIRE_NUMBER_OF_DEVICES] = {0};
    average_array[device][index[device]] = temperature;
    index[device] = (index[device] + 1) % AVERAGE_ARRAY_SIZE;

    size_t count = 0;
    float average = 0.0;
    for (size_t i = 0; i < AVERAGE_ARRAY_SIZE; ++i) {
        if (!isnan(average_array[device][i])) {
            average += average_array[device][i];
            count++;
        }
    }
    return count > 0 ? average / (float)count : NAN;
}

#define CHANGE_THRESHOLD 0.09  // °C
static bool is_temperature_changed(size_t device, float current_temperature)
{
    static float previous_temperature[CONFIG_ONEWIRE_NUMBER_OF_DEVICES] = {0.0};

    bool is_changed = fabs(current_temperature - previous_temperature[device]) > (float)CHANGE_THRESHOLD;
    if (is_changed) {
        previous_temperature[device] = current_temperature;
    }
    return is_changed;
}

static void ds18b20_task(void *params)
{
    // convert and read temperature
    while (true) {
        esp_err_t err;
        ds18b20_task_params_t task_params = *(ds18b20_task_params_t*)params;

        vTaskDelay(pdMS_TO_TICKS(200));

        // set all sensors' temperature conversion resolution
        err = ds18b20_set_resolution(task_params.handle, NULL, DS18B20_RESOLUTION_12B);
        if (err != ESP_OK) {
            continue;
        }

        // trigger all sensors to start temperature conversion
        err = ds18b20_trigger_temperature_conversion(task_params.handle, NULL); // skip rom to send command to all devices on the bus
        if (err != ESP_OK) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(800)); // 12-bit resolution needs 750ms to convert

        // get temperature from sensors
        for (size_t device = 0; device < task_params.device_num; ++device) {
            float temperature;
            err = ds18b20_get_temperature(task_params.handle, task_params.device_rom_id[device], &temperature); // read scratchpad and get temperature
            if (err != ESP_OK) {
                continue;
            }
            ESP_LOGI(TAG, "Temperature of device " ONEWIRE_ROM_ID_STR ": %.2f°C",
                     ONEWIRE_ROM_ID(task_params.device_rom_id[device]), temperature);

            temperature = get_average_temperature(device, temperature);

            if (is_temperature_changed(device, temperature)) {
                temperature_device_t temperature_device_to_send = {
                    .device = device,
                    .temperature = temperature,
                };

                BaseType_t status = xQueueSend(temperature_queue, &temperature_device_to_send, 0);
                if (status != pdPASS) {
                    ESP_LOGW(TAG, "ds18b20_task(): Failed to send the message");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_ONEWIRE_TEMPERATURE_UPDATE_TIME * 1000));
    }
}

esp_err_t ds18b20_init(void)
{
    onewire_rmt_config_t config = {
        .gpio_pin = CONFIG_ONEWIRE_DATA_GPIO_PIN,
        .max_rx_bytes = 10, // 10 tx bytes (1byte ROM command + 8byte ROM number + 1byte device command)
    };

    // install new 1-wire bus

    // NOTE: The parameter "task_params" must still exist when the created task executes. It must be static.
    static ds18b20_task_params_t task_params = {
        .device_num = 0,
    };

    ESP_ERROR_CHECK(onewire_new_bus_rmt(&config, &task_params.handle));
    ESP_LOGI(TAG, "1-wire bus installed");

    // create 1-wire rom search context
    onewire_rom_search_context_handler_t context_handler;
    ESP_ERROR_CHECK(onewire_rom_search_context_create(task_params.handle, &context_handler));

    // search for devices on the bus
    do {
        esp_err_t search_result = onewire_rom_search(context_handler);

        if (search_result == ESP_ERR_INVALID_CRC) {
            continue; // continue on crc error
        } else if (search_result == ESP_FAIL || search_result == ESP_ERR_NOT_FOUND) {
            break; // break on finish or no device
        }

        ESP_ERROR_CHECK(onewire_rom_get_number(context_handler, task_params.device_rom_id[task_params.device_num]));
        ESP_LOGI(TAG, "found device with rom id " ONEWIRE_ROM_ID_STR,
                 ONEWIRE_ROM_ID(task_params.device_rom_id[task_params.device_num]));
        task_params.device_num++;
    } while (task_params.device_num < CONFIG_ONEWIRE_NUMBER_OF_DEVICES);

    // delete 1-wire rom search context
    ESP_ERROR_CHECK(onewire_rom_search_context_delete(context_handler));
    ESP_LOGI(TAG, "%d device%s found on 1-wire bus", task_params.device_num, task_params.device_num > 1 ? "s" : "");

    if (task_params.device_num > 0) {   
        temperature_queue = xQueueCreate(task_params.device_num, sizeof(temperature_device_t));
        if (temperature_queue == NULL) {
            ESP_LOGE(TAG, "temperature_queue: Queue was not created. Could not allocate required memory");
            return ESP_ERR_NO_MEM;
        }
        
        BaseType_t status = xTaskCreate(ds18b20_task, "ds18b20_task", configMINIMAL_STACK_SIZE * 4, &task_params,
                                        PRIORITY_HIGH, NULL);
        if (status != pdPASS) {
            ESP_LOGE(TAG, "ds18b20_task(): Task was not created. Could not allocate required memory");
            return ESP_ERR_NO_MEM;
        }
    } else {
        ESP_ERROR_CHECK(onewire_del_bus(task_params.handle));
        ESP_LOGI(TAG, "1-wire bus deleted");
    }

    ESP_LOGI(TAG, "ds18b20_init() finished");
    return ESP_OK;
}
