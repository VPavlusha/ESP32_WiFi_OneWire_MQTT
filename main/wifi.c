#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_timer.h"
#include <esp_wifi.h>
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "wifi";

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t wifi_event_group = NULL;

// The event group allows multiple bits for each event, but we only care about two events:
// - we are connected to the AP with an IP
// - we failed to connect after the maximum amount of retries
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static esp_timer_handle_t wifi_reconnect_timer = NULL;

static void wifi_reconnect_timer_callback(void* arg)
{
    esp_wifi_connect();
    ESP_LOGW(TAG, "Retry to connect to the Wi-Fi");
}

static void wifi_reconnect_timer_start(void)
{
    if (wifi_reconnect_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = wifi_reconnect_timer_callback,
            .arg = NULL,
            .name = "Wi-Fi reconnect timer"
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &wifi_reconnect_timer));
    }
    ESP_ERROR_CHECK(esp_timer_start_once(wifi_reconnect_timer, 5 * 1000000)); // 5 seconds
}

static void wifi_reconnect_timer_stop(void)
{
    if (wifi_reconnect_timer != NULL) {
        if (esp_timer_is_active(wifi_reconnect_timer)) {
            ESP_ERROR_CHECK(esp_timer_stop(wifi_reconnect_timer));
        }
        ESP_ERROR_CHECK(esp_timer_delete(wifi_reconnect_timer));
        wifi_reconnect_timer = NULL;
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI(TAG, "Trying to connect to the Wi-Fi");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (wifi_event_group != NULL) {
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                wifi_reconnect_timer_start();
                break;       
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                ESP_LOGI(TAG, "Connected to the Wi-Fi. Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
                if (wifi_event_group != NULL) {
                    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
                } else {
                    wifi_reconnect_timer_stop();
                }
                break;           
            default:
                break;
        }
    }
}

esp_err_t wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "wifi_event_group: Event Group was not created. Could not allocate required memory");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            // Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
            // If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
            // to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	        // WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
    // number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above).
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    // xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event
    // actually happened.
    if (bits & WIFI_CONNECTED_BIT) {
        //ESP_LOGI(TAG, "Connected to AP. SSID:%s, password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
        ESP_LOGI(TAG, "Connected to the Wi-Fi. SSID:%s, password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
    } else if (bits & WIFI_FAIL_BIT) {
        //ESP_LOGE(TAG, "Failed to connect to AP. SSID:%s, password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
        ESP_LOGE(TAG, "Failed to connect to the Wi-Fi. SSID:%s, password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
    } else {
        ESP_LOGE(TAG, "Unexpected event");
    }
    vEventGroupDelete(wifi_event_group);
    wifi_event_group = NULL;

    ESP_LOGI(TAG, "wifi_init() finished");
    return ESP_OK;
}
