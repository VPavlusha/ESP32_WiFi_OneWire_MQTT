#include "mqtt.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_err.h"
#include "esp_log.h"

#include "mqtt_client.h"

#include "led.h"
#include "temperature.h"
#include "types.h"

static const char *TAG = "mqtt";

typedef enum {
    MQTT_RETAIN_TRUE  = true,
    MQTT_RETAIN_FALSE = false,
} mqtt_retain_t;

static const char TOPIC_LED_SWITCH[]  = CONFIG_BROKER_TOPIC_PREFIX "/led_switch";
static const char TOPIC_LED_STATUS[]  = CONFIG_BROKER_TOPIC_PREFIX "/led_status";
static const char TOPIC_TEMPERATURE[] = CONFIG_BROKER_TOPIC_PREFIX "/temperature/device_";

static TaskHandle_t mqtt_task_handle = NULL;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static inline bool is_event_group_created(EventGroupHandle_t event)
{
    return event != NULL ? true : false;
}

static void handle_data(void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    if (*(event->topic) == *TOPIC_LED_SWITCH) {
        //xEventGroupSetBits(led_event_group, LED_EVENT_BLINK);
        if (*(event->data) == '1') {
            if (is_event_group_created(led_event_group)) {
                xEventGroupSetBits(led_event_group, LED_EVENT_ON);
                esp_mqtt_client_publish(client, TOPIC_LED_STATUS, "1", 0, 1, MQTT_RETAIN_TRUE);
                ESP_LOGI(TAG, "led_event_group - LED_EVENT_ON");
            }
        } else if (*(event->data) == '0') {
            if (is_event_group_created(led_event_group)) {
                xEventGroupSetBits(led_event_group, LED_EVENT_OFF);
                esp_mqtt_client_publish(client, TOPIC_LED_STATUS, "0", 0, 1, MQTT_RETAIN_TRUE);
                ESP_LOGI(TAG, "led_event_group - LED_EVENT_OFF");
            }
        }
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%lu", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        vTaskResume(mqtt_task_handle);
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, TOPIC_LED_SWITCH, 0);
        ESP_LOGI(TAG, "Sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        vTaskSuspend(mqtt_task_handle);
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA=%.*s\r\n", event->data_len, event->data);

        handle_data(event_data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("Reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("Reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("Captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGE(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGW(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static const char* float_to_string(float number, char *string)
{
    sprintf(string, "%.1f", number);  // Convert float to string with one decimal place
    return string;
}

static const char* get_topic(char *topic, int device)
{
    sprintf(topic, "%s%d", TOPIC_TEMPERATURE, device);
    return topic;
}

static inline bool is_queue_created(QueueHandle_t queue)
{
    return queue != NULL ? true : false;
}

static void mqtt_task(void *params)
{
    while (true) {
        if (is_queue_created(temperature_queue)) {
            temperature_device_t received_value;
            BaseType_t status = xQueueReceive(temperature_queue, &received_value, portMAX_DELAY);

            if (status == pdPASS) {
                char topic[sizeof(TOPIC_TEMPERATURE) + 3 * sizeof(char)];  // 3 chars for number 128 (max devices)
                char string[20];  // 20 - maximum number of characters for a float: -[sign][d].[d...]e[sign]d
                const esp_mqtt_client_handle_t client = *(esp_mqtt_client_handle_t*)params;

                esp_mqtt_client_publish(client, get_topic(topic, received_value.device),
                                        float_to_string(received_value.temperature, string), 0, 1, MQTT_RETAIN_TRUE);
            } else {
                ESP_LOGE(TAG, "mqtt_task(): Failed to receive the message from the temperature_queue");
            }
        } else {
            ESP_LOGE(TAG, "The temperature_queue has not been created yet");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

esp_err_t mqtt_init(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
        .broker.address.port = CONFIG_BROKER_PORT,
    };

    // NOTE: The parameter "client" must still exist when the created task executes. It must be static.
    static esp_mqtt_client_handle_t client;
    client = esp_mqtt_client_init(&mqtt_cfg);

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    BaseType_t status = xTaskCreate(mqtt_task, "mqtt_task", configMINIMAL_STACK_SIZE * 4, &client, PRIORITY_MIDDLE,
                                    &mqtt_task_handle);
    if (status != pdPASS) {
        ESP_LOGE(TAG, "mqtt_task(): Task was not created. Could not allocate required memory");
        return ESP_ERR_NO_MEM;
    }
    vTaskSuspend(mqtt_task_handle);

    ESP_LOGI(TAG, "mqtt_init() finished successfully");

    return ESP_OK;
}
