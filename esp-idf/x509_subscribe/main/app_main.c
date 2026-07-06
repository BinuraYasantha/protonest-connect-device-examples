#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "mbedtls/x509_crt.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "Config.h"

static const char *TAG = "protonest_x509_subscribe";

extern const uint8_t root_ca_crt_start[] asm("_binary_root_ca_crt_start");
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_key_pem_start[] asm("_binary_device_key_pem_start");

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

static bool extract_certificate_common_name(char *buffer, size_t buffer_size)
{
    mbedtls_x509_crt cert;
    char subject[256];
    const char *cn_start;
    const char *cn_end;
    size_t cn_length;
    int ret;

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    mbedtls_x509_crt_init(&cert);
    ret = mbedtls_x509_crt_parse(&cert, device_cert_pem_start, strlen((const char *)device_cert_pem_start) + 1U);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse device certificate (mbedtls: %d)", ret);
        mbedtls_x509_crt_free(&cert);
        return false;
    }

    ret = mbedtls_x509_dn_gets(subject, sizeof(subject), &cert.subject);
    mbedtls_x509_crt_free(&cert);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to read device certificate subject");
        return false;
    }

    cn_start = strstr(subject, "CN=");
    if (cn_start == NULL) {
        ESP_LOGE(TAG, "Device certificate subject does not contain a common name");
        return false;
    }

    cn_start += 3;
    cn_end = strchr(cn_start, ',');
    if (cn_end == NULL) {
        cn_end = cn_start + strlen(cn_start);
    }

    cn_length = (size_t)(cn_end - cn_start);
    if (cn_length >= buffer_size) {
        cn_length = buffer_size - 1U;
    }

    memcpy(buffer, cn_start, cn_length);
    buffer[cn_length] = '\0';
    return true;
}

static bool validate_device_identity(void)
{
    char certificate_cn[128];

    if (!extract_certificate_common_name(certificate_cn, sizeof(certificate_cn))) {
        return false;
    }

    ESP_LOGI(TAG, "Device certificate CN: %s", certificate_cn);

    if (strcmp(certificate_cn, DEVICE_NAME) != 0) {
        ESP_LOGE(TAG,
                 "Device name mismatch: Config.h uses '%s' but the embedded certificate CN is '%s'",
                 DEVICE_NAME,
                 certificate_cn);
        ESP_LOGE(TAG, "For X.509 auth, DEVICE_NAME, MQTT client_id, and certificate identity must match.");
        return false;
    }

    return true;
}

static void build_state_topic(char *buffer, size_t buffer_size, const char *state_name)
{
    snprintf(buffer, buffer_size, "protonest/%s/state/%s", DEVICE_NAME, state_name);
}

static bool is_expected_command_topic(const char *topic)
{
    char expected_topic[128];
    build_state_topic(expected_topic, sizeof(expected_topic), PROTONEST_STATE_NAME);
    return strcmp(topic, expected_topic) == 0;
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

static esp_err_t sync_clock(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time_t now = 0;
    int retry = 0;
    const int retry_count = 20;

    ESP_LOGI(TAG, "Syncing clock...");
    esp_netif_sntp_init(&config);

    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }

    time(&now);
    esp_netif_sntp_deinit();

    if (now <= 1700000000) {
        ESP_LOGE(TAG, "Clock sync failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Clock synced: %" PRIi64, (int64_t)now);
    return ESP_OK;
}

static void publish_ack(const char *payload)
{
    char ack_topic[128];
    build_state_topic(ack_topic, sizeof(ack_topic), PROTONEST_ACK_STATE_NAME);

    const int message_id = esp_mqtt_client_publish(s_mqtt_client, ack_topic, payload, 0, 1, 0);
    ESP_LOGI(TAG,
             "Ack %s -> %s (%s, msg_id=%d)",
             ack_topic,
             payload,
             message_id >= 0 ? "queued" : "failed",
             message_id);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        char subscribe_topic[128];
        build_state_topic(subscribe_topic, sizeof(subscribe_topic), PROTONEST_STATE_NAME);

        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGI(TAG, "MQTT connected");

        const int message_id = esp_mqtt_client_subscribe(s_mqtt_client, subscribe_topic, 1);
        ESP_LOGI(TAG,
                 "Subscribed to %s (%s, msg_id=%d)",
                 subscribe_topic,
                 message_id >= 0 ? "ok" : "failed",
                 message_id);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Broker confirmed subscription, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA: {
        char topic[128];
        char payload[256];
        size_t topic_length = (size_t)event->topic_len;
        size_t payload_length = (size_t)event->data_len;

        if (topic_length >= sizeof(topic)) {
            topic_length = sizeof(topic) - 1;
        }
        memcpy(topic, event->topic, topic_length);
        topic[topic_length] = '\0';

        if (payload_length >= sizeof(payload)) {
            payload_length = sizeof(payload) - 1;
        }
        memcpy(payload, event->data, payload_length);
        payload[payload_length] = '\0';

        ESP_LOGI(TAG, "Incoming %s -> %s", topic, payload);

        if (!is_expected_command_topic(topic)) {
            char expected_topic[128];
            build_state_topic(expected_topic, sizeof(expected_topic), PROTONEST_STATE_NAME);
            ESP_LOGW(TAG,
                     "Message received on non-command topic, expected %s, skipping ack",
                     expected_topic);
            break;
        }

        publish_ack(payload);
        break;
    }
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
    ESP_LOGI(TAG,
             "Connecting to MQTT broker %s:%d as %s",
             PROTONEST_MQTT_HOST,
             PROTONEST_MQTT_PORT,
             DEVICE_NAME);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = PROTONEST_MQTT_URI,
            .verification.certificate = (const char *)root_ca_crt_start,
        },
        .credentials = {
            .client_id = DEVICE_NAME,
            .authentication = {
                .certificate = (const char *)device_cert_pem_start,
                .key = (const char *)device_key_pem_start,
            },
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

void app_main(void)
{
    configure_log_levels();

    ESP_LOGI(TAG, "Protonest Connect ESP-IDF X.509 subscribe example");
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "MQTT client_id == device name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Subscribe topic: protonest/%s/state/%s", DEVICE_NAME, PROTONEST_STATE_NAME);
    ESP_LOGI(TAG, "Ack topic: protonest/%s/state/%s", DEVICE_NAME, PROTONEST_ACK_STATE_NAME);
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    if (!validate_device_identity()) {
        ESP_LOGE(TAG, "Stopping before Wi-Fi startup because device identity validation failed.");
        vTaskDelete(NULL);
    }

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

    ret = sync_clock();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stopping before MQTT startup because clock sync failed.");
        vTaskDelete(NULL);
    }

    mqtt_start();
    vTaskDelete(NULL);
}
