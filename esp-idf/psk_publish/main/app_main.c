#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "Config.h"

static const char *TAG = "protonest_psk_publish";

extern const uint8_t root_ca_crt_start[] asm("_binary_root_ca_crt_start");
extern const uint8_t root_ca_crt_end[] asm("_binary_root_ca_crt_end");

static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t s_mqtt_event_group;
static esp_mqtt_client_handle_t s_mqtt_client;
static int s_wifi_retry_num = 0;

static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;
static const int MQTT_CONNECTED_BIT = BIT0;

static void configure_log_levels(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("wifi_init", ESP_LOG_ERROR);
    esp_log_level_set("phy_init", ESP_LOG_ERROR);
    esp_log_level_set("net80211", ESP_LOG_ERROR);
    esp_log_level_set("pp", ESP_LOG_ERROR);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_ERROR);
    esp_log_level_set("transport_base", ESP_LOG_ERROR);
    esp_log_level_set("mqtt_client", ESP_LOG_ERROR);
    esp_log_level_set("esp-tls", ESP_LOG_ERROR);
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_ERROR);
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_ERROR);
}

static void build_stream_topic(char *buffer, size_t buffer_size, const char *stream_name)
{
    snprintf(buffer,
             buffer_size,
             "protonest/%s/stream/%s",
             DEVICE_NAME,
             stream_name);
}

static void build_publish_payload(char *buffer, size_t buffer_size)
{
    const uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
    const float sample_temperature = 23.5f + (float)((uptime_ms / 1000ULL) % 20ULL) / 10.0f;

    snprintf(buffer,
             buffer_size,
             "{\"temperature_c\":%.1f,\"uptime_ms\":%" PRIu64 "}",
             sample_temperature,
             uptime_ms);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);

        if (s_wifi_retry_num < PROTONEST_WIFI_MAXIMUM_RETRY) {
            s_wifi_retry_num++;
            ESP_LOGW(TAG, "Wi-Fi disconnected, retrying (%d/%d)", s_wifi_retry_num, PROTONEST_WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_wifi_retry_num = 0;
        ESP_LOGI(TAG, "Wi-Fi connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to connect to Wi-Fi after %d retries", PROTONEST_WIFI_MAXIMUM_RETRY);
    return ESP_FAIL;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_ERROR:
        xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGE(TAG, "MQTT error");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "esp-tls last error: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG, "tls stack error: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGE(TAG, "socket errno: %d", event->error_handle->esp_transport_sock_errno);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "broker refused connection: 0x%x", event->error_handle->connect_return_code);
        }
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    ESP_LOGI(TAG, "Connecting to MQTT broker %s:%d", PROTONEST_MQTT_HOST, PROTONEST_MQTT_PORT);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = PROTONEST_MQTT_URI,
            .verification.certificate = (const char *)root_ca_crt_start,
        },
        .credentials = {
            .username = DEVICE_NAME,
            .client_id = DEVICE_NAME,
            .authentication.password = MQTT_PASSWORD,
        },
        .network = {
            .reconnect_timeout_ms = PROTONEST_MQTT_RECONNECT_DELAY_MS,
            .timeout_ms = 10000,
        },
        .session = {
            .keepalive = 120,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
}

static void publish_sample(void)
{
    char topic[128];
    char payload[128];

    build_stream_topic(topic, sizeof(topic), PROTONEST_STREAM_NAME);
    build_publish_payload(payload, sizeof(payload));

    const int message_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
    ESP_LOGI(TAG,
             "Publish %s -> %s (%s, msg_id=%d)",
             topic,
             payload,
             message_id >= 0 ? "queued" : "failed",
             message_id);
}

static void publisher_task(void *arg)
{
    while (true) {
        xEventGroupWaitBits(s_mqtt_event_group,
                            MQTT_CONNECTED_BIT,
                            pdFALSE,
                            pdFALSE,
                            portMAX_DELAY);

        publish_sample();
        vTaskDelay(pdMS_TO_TICKS(PROTONEST_PUBLISH_INTERVAL_MS));
    }
}

void app_main(void)
{
    configure_log_levels();

    ESP_LOGI(TAG, "Protonest Connect ESP-IDF PSK publish example");
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "MQTT client_id == username == device name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Stream topic: protonest/%s/stream/%s", DEVICE_NAME, PROTONEST_STREAM_NAME);
    ESP_LOGI(TAG, "Publish interval: %d ms", PROTONEST_PUBLISH_INTERVAL_MS);
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    s_mqtt_event_group = xEventGroupCreate();
    if (s_mqtt_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT event group");
        vTaskDelete(NULL);
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi setup failed. Check SSID, password, and AP availability.");
        ESP_LOGI(TAG, "Stopping before MQTT startup.");
        vTaskDelete(NULL);
    }

    mqtt_start();

    BaseType_t task_created = xTaskCreate(publisher_task,
                                          "protonest_publish",
                                          4096,
                                          NULL,
                                          5,
                                          NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create publisher task");
        vTaskDelete(NULL);
    }

    vTaskDelete(NULL);
}
