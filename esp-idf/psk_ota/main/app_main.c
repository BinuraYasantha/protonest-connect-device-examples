#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "Config.h"

static const char *TAG = "protonest_psk_ota";

extern const uint8_t root_ca_crt_start[] asm("_binary_root_ca_crt_start");
extern const uint8_t http_root_ca_pem_start[] asm("_binary_http_root_ca_pem_start");

static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t s_mqtt_event_group;
static esp_mqtt_client_handle_t s_mqtt_client;
static int s_wifi_retry_num = 0;
static int s_last_handled_ota_id = -1;
static bool s_ota_in_progress = false;
static bool s_ota_storage_ready = false;
static bool s_pending_firmware_needs_confirmation = false;
static bool s_pending_firmware_confirmed = false;
static bool s_bootloader_rollback_pending = false;
static int64_t s_pending_firmware_start_ms = 0;
static nvs_handle_t s_ota_nvs;
static char *s_incoming_payload = NULL;
static int s_incoming_payload_total_len = 0;
static char s_incoming_topic[128];

static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;
static const int MQTT_CONNECTED_BIT = BIT0;

enum {
    OTA_VERSION_LEN = 64,
    OTA_URL_LEN = 512,
    OTA_STATUS_BUFFER_LEN = 320,
};

typedef struct {
    int32_t ota_id;
    char version[OTA_VERSION_LEN];
} ota_metadata_record_t;

typedef struct {
    int ota_id;
    int expected_size;
    char version[OTA_VERSION_LEN];
    char url[OTA_URL_LEN];
} ota_request_t;

static ota_metadata_record_t s_current_ota_record = {-1, ""};
static ota_metadata_record_t s_previous_ota_record = {-1, ""};
static ota_metadata_record_t s_pending_ota_record = {-1, ""};

static bool ota_record_is_set(const ota_metadata_record_t *record)
{
    return record->ota_id >= 0 || record->version[0] != '\0';
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

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
    esp_log_level_set("esp_http_client", ESP_LOG_ERROR);
    esp_log_level_set("esp_https_ota", ESP_LOG_ERROR);
    esp_log_level_set("transport_base", ESP_LOG_ERROR);
    esp_log_level_set("mqtt_client", ESP_LOG_ERROR);
    esp_log_level_set("esp-tls", ESP_LOG_ERROR);
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_ERROR);
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_ERROR);
}

static void print_progress_bar(size_t current, size_t total)
{
    const size_t bar_width = 32;
    char bar[33];
    size_t filled = total > 0 ? (current * bar_width) / total : 0;
    if (filled > bar_width) {
        filled = bar_width;
    }

    for (size_t index = 0; index < bar_width; ++index) {
        bar[index] = index < filled ? '#' : '.';
    }
    bar[bar_width] = '\0';

    unsigned int percent = total > 0 ? (unsigned int)((current * 100U) / total) : 0U;
    if (percent > 100U) {
        percent = 100U;
    }

    ESP_LOGI(TAG,
             "Downloading firmware [%s] %3u%% (%u/%u bytes)",
             bar,
             percent,
             (unsigned int)current,
             (unsigned int)total);
}

static void blink_led_non_blocking(gpio_num_t pin, uint32_t interval_ms)
{
    static int64_t previous_ms = 0;
    static bool led_state = false;
    int64_t current_ms = now_ms();

    if (current_ms - previous_ms >= (int64_t)interval_ms) {
        previous_ms = current_ms;
        led_state = !led_state;
        gpio_set_level(pin, led_state ? 1 : 0);
    }
}

static void build_ota_pending_topic(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, "protonest/%s/ota/pending", DEVICE_NAME);
}

static void build_ota_status_topic(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, "protonest/%s/ota/status/update", DEVICE_NAME);
}

static void normalize_version_string(const char *version, char *buffer, size_t buffer_size)
{
    if (version != NULL && version[0] != '\0') {
        snprintf(buffer, buffer_size, "%s", version);
        return;
    }

    snprintf(buffer, buffer_size, "%s", PROTONEST_APP_VERSION);
}

static bool open_ota_storage(void)
{
    if (s_ota_storage_ready) {
        return true;
    }

    esp_err_t err = nvs_open("pn_ota", NVS_READWRITE, &s_ota_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open OTA metadata storage: %s", esp_err_to_name(err));
        return false;
    }

    s_ota_storage_ready = true;
    return true;
}

static void load_ota_record(const char *id_key, const char *version_key, ota_metadata_record_t *record)
{
    record->ota_id = -1;
    record->version[0] = '\0';

    nvs_get_i32(s_ota_nvs, id_key, &record->ota_id);

    size_t version_length = sizeof(record->version);
    esp_err_t err = nvs_get_str(s_ota_nvs, version_key, record->version, &version_length);
    if (err != ESP_OK) {
        record->version[0] = '\0';
    }
}

static bool save_ota_record(const char *id_key,
                            const char *version_key,
                            ota_metadata_record_t *target,
                            const ota_metadata_record_t *value)
{
    esp_err_t err = nvs_set_i32(s_ota_nvs, id_key, value->ota_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save %s: %s", id_key, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(s_ota_nvs, version_key, value->version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save %s: %s", version_key, esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(s_ota_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit OTA metadata: %s", esp_err_to_name(err));
        return false;
    }

    *target = *value;
    return true;
}

static bool clear_ota_record(const char *id_key, const char *version_key, ota_metadata_record_t *target)
{
    esp_err_t err = nvs_erase_key(s_ota_nvs, id_key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to clear %s: %s", id_key, esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_key(s_ota_nvs, version_key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to clear %s: %s", version_key, esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(s_ota_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit OTA metadata clear: %s", esp_err_to_name(err));
        return false;
    }

    target->ota_id = -1;
    target->version[0] = '\0';
    return true;
}

static bool clear_pending_ota_record(void)
{
    if (!s_ota_storage_ready || !ota_record_is_set(&s_pending_ota_record)) {
        return true;
    }

    return clear_ota_record("pend_id", "pend_ver", &s_pending_ota_record);
}

static bool promote_pending_ota_record(void)
{
    ota_metadata_record_t confirmed_record = s_pending_ota_record;
    if (!ota_record_is_set(&confirmed_record)) {
        return true;
    }

    if (confirmed_record.version[0] == '\0') {
        normalize_version_string(NULL, confirmed_record.version, sizeof(confirmed_record.version));
    }

    if (!save_ota_record("prev_id", "prev_ver", &s_previous_ota_record, &s_current_ota_record)) {
        return false;
    }

    if (!save_ota_record("cur_id", "cur_ver", &s_current_ota_record, &confirmed_record)) {
        return false;
    }

    return clear_pending_ota_record();
}

static bool stage_pending_ota_record(int ota_id, const char *version)
{
    ota_metadata_record_t pending_record = {.ota_id = ota_id, .version = ""};

    if (!open_ota_storage()) {
        return false;
    }

    normalize_version_string(version, pending_record.version, sizeof(pending_record.version));
    return save_ota_record("pend_id", "pend_ver", &s_pending_ota_record, &pending_record);
}

static void load_ota_metadata(void)
{
    char running_version[OTA_VERSION_LEN];
    bool current_matches_running;
    bool pending_matches_running;

    if (!open_ota_storage()) {
        return;
    }

    load_ota_record("cur_id", "cur_ver", &s_current_ota_record);
    load_ota_record("prev_id", "prev_ver", &s_previous_ota_record);
    load_ota_record("pend_id", "pend_ver", &s_pending_ota_record);

    normalize_version_string(NULL, running_version, sizeof(running_version));
    current_matches_running = strcmp(s_current_ota_record.version, running_version) == 0;
    pending_matches_running = strcmp(s_pending_ota_record.version, running_version) == 0;

    if (!ota_record_is_set(&s_current_ota_record)) {
        s_current_ota_record.ota_id = -1;
        snprintf(s_current_ota_record.version, sizeof(s_current_ota_record.version), "%s", running_version);
        save_ota_record("cur_id", "cur_ver", &s_current_ota_record, &s_current_ota_record);
        current_matches_running = true;
    }

    if (s_bootloader_rollback_pending || pending_matches_running) {
        s_pending_firmware_needs_confirmation = true;
        s_pending_firmware_start_ms = now_ms();

        if (s_bootloader_rollback_pending) {
            ESP_LOGI(TAG, "New OTA image is pending verification");
        }

        if (ota_record_is_set(&s_pending_ota_record)) {
            ESP_LOGI(TAG,
                     "Pending OTA metadata: otaId=%" PRId32 " version=%s",
                     s_pending_ota_record.ota_id,
                     s_pending_ota_record.version);
        }

        ESP_LOGI(TAG, "Waiting for post-boot health check before confirming firmware");
        return;
    }

    if (ota_record_is_set(&s_pending_ota_record) && current_matches_running) {
        ESP_LOGW(TAG, "Clearing stale pending OTA metadata");
        clear_pending_ota_record();
    }

    if (!current_matches_running && !ota_record_is_set(&s_pending_ota_record)) {
        save_ota_record("prev_id", "prev_ver", &s_previous_ota_record, &s_current_ota_record);

        s_current_ota_record.ota_id = -1;
        snprintf(s_current_ota_record.version, sizeof(s_current_ota_record.version), "%s", running_version);
        save_ota_record("cur_id", "cur_ver", &s_current_ota_record, &s_current_ota_record);
    }
}

static bool ota_id_is_allowed(int ota_id)
{
    if (ota_record_is_set(&s_pending_ota_record) && ota_id == s_pending_ota_record.ota_id) {
        ESP_LOGW(TAG, "Rejecting otaId=%d because it is already pending verification", ota_id);
        return false;
    }

    if (s_current_ota_record.ota_id >= 0 && ota_id <= s_current_ota_record.ota_id) {
        ESP_LOGW(TAG,
                 "Rejecting otaId=%d because current confirmed otaId=%" PRId32,
                 ota_id,
                 s_current_ota_record.ota_id);
        return false;
    }

    if (s_previous_ota_record.ota_id >= 0 && ota_id == s_previous_ota_record.ota_id) {
        ESP_LOGW(TAG,
                 "Rejecting otaId=%d because it matches previous confirmed otaId=%" PRId32,
                 ota_id,
                 s_previous_ota_record.ota_id);
        return false;
    }

    return true;
}

static bool should_ignore_ota_id(int ota_id)
{
    if (s_current_ota_record.ota_id >= 0 && ota_id <= s_current_ota_record.ota_id) {
        return true;
    }

    if (s_previous_ota_record.ota_id >= 0 && ota_id == s_previous_ota_record.ota_id) {
        return true;
    }

    return false;
}

static void init_rollback_verification(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return;
    }

    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) != ESP_OK) {
        return;
    }

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_bootloader_rollback_pending = true;
    }
}

static void publish_ota_status(const char *status, int ota_id, const char *reason)
{
    char topic[128];
    char payload[OTA_STATUS_BUFFER_LEN];
    int message_id;

    if (reason != NULL && reason[0] != '\0') {
        ESP_LOGW(TAG, "OTA status %s reason: %s", status, reason);
    }

    build_ota_status_topic(topic, sizeof(topic));

    snprintf(payload, sizeof(payload), "{\"status\":\"%s\",\"otaId\":\"%d\"}", status, ota_id);

    message_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
    if (message_id < 0) {
        ESP_LOGW(TAG, "Failed to publish OTA status to %s", topic);
        return;
    }

    ESP_LOGI(TAG, "Publish %s -> %s (queued, msg_id=%d)", topic, payload, message_id);
}

static void health_monitor_task(void *arg)
{
    while (s_pending_firmware_needs_confirmation && !s_pending_firmware_confirmed) {
        EventBits_t wifi_bits = xEventGroupGetBits(s_wifi_event_group);
        EventBits_t mqtt_bits = xEventGroupGetBits(s_mqtt_event_group);
        int64_t elapsed_ms = now_ms() - s_pending_firmware_start_ms;
        bool wifi_ok = (wifi_bits & WIFI_CONNECTED_BIT) != 0;
        bool mqtt_ok = (mqtt_bits & MQTT_CONNECTED_BIT) != 0;

        if (wifi_ok && mqtt_ok && elapsed_ms >= PROTONEST_ROLLBACK_CONFIRM_DELAY_MS) {
            const int confirmed_ota_id = s_pending_ota_record.ota_id;

            ESP_LOGI(TAG, "Post-boot health check passed");

            if (s_bootloader_rollback_pending) {
                esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to confirm firmware: %s", esp_err_to_name(err));
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }

                ESP_LOGI(TAG, "Firmware marked valid. Rollback cancelled.");
            }

            if (!promote_pending_ota_record()) {
                ESP_LOGE(TAG, "Failed to promote pending OTA metadata");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            if (confirmed_ota_id >= 0) {
                publish_ota_status("completed", confirmed_ota_id, NULL);
                vTaskDelay(pdMS_TO_TICKS(PROTONEST_STATUS_PUBLISH_DELAY_MS));
            }

            s_pending_firmware_needs_confirmation = false;
            s_pending_firmware_confirmed = true;
            break;
        }

        if (s_bootloader_rollback_pending && elapsed_ms >= PROTONEST_ROLLBACK_VERIFY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "Post-boot health check timed out");
            ESP_LOGE(TAG, "Rolling back to previous firmware");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

static void led_blink_task(void *arg)
{
    const gpio_num_t led_pin = (gpio_num_t)PROTONEST_LED_PIN;

    while (true) {
        blink_led_non_blocking(led_pin, PROTONEST_LED_BLINK_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
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

static void cleanup_incoming_payload(void)
{
    free(s_incoming_payload);
    s_incoming_payload = NULL;
    s_incoming_payload_total_len = 0;
    s_incoming_topic[0] = '\0';
}

static void abort_ota_handle(esp_https_ota_handle_t *handle)
{
    if (handle != NULL && *handle != NULL) {
        esp_https_ota_abort(*handle);
        *handle = NULL;
    }
}

static void finish_failed_ota(esp_https_ota_handle_t *handle, int ota_id, const char *reason)
{
    abort_ota_handle(handle);
    clear_pending_ota_record();
    publish_ota_status("failed", ota_id, reason);
    s_ota_in_progress = false;
}

static void run_ota_request(const ota_request_t *request)
{
    esp_http_client_config_t http_config = {
        .url = request->url,
        .cert_pem = (const char *)http_root_ca_pem_start,
        .timeout_ms = 12000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };
    esp_https_ota_handle_t ota_handle = NULL;
    esp_app_desc_t new_app_info;
    esp_err_t err;
    int remote_size;
    int last_percent = -1;

    ESP_LOGI(TAG,
             "Starting OTA update id=%d version=%s url=%s",
             request->ota_id,
             request->version,
             request->url);

    err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS OTA begin failed: %s", esp_err_to_name(err));
        finish_failed_ota(&ota_handle, request->ota_id, "http_begin_failed");
        return;
    }

    err = esp_https_ota_get_img_desc(ota_handle, &new_app_info);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Image version from firmware header: %s", new_app_info.version);
    } else {
        ESP_LOGW(TAG, "Unable to read image description: %s", esp_err_to_name(err));
    }

    remote_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "Expected size=%d, remote size=%d", request->expected_size, remote_size);

    if (request->expected_size > 0 && remote_size > 0 && request->expected_size != remote_size) {
        ESP_LOGE(TAG, "OTA size mismatch");
        finish_failed_ota(&ota_handle, request->ota_id, "size_mismatch");
        return;
    }

    while ((err = esp_https_ota_perform(ota_handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int bytes_read = esp_https_ota_get_image_len_read(ota_handle);

        if (remote_size > 0) {
            int percent = (bytes_read * 100) / remote_size;
            if (percent != last_percent) {
                print_progress_bar((size_t)bytes_read, (size_t)remote_size);
                last_percent = percent;
            }
        } else {
            ESP_LOGI(TAG, "Downloaded %d bytes", bytes_read);
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS OTA perform failed: %s", esp_err_to_name(err));
        finish_failed_ota(&ota_handle, request->ota_id, "download_failed");
        return;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "OTA image download incomplete");
        finish_failed_ota(&ota_handle, request->ota_id, "incomplete_data");
        return;
    }

    err = esp_https_ota_finish(ota_handle);
    ota_handle = NULL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS OTA finish failed: %s", esp_err_to_name(err));
        clear_pending_ota_record();
        publish_ota_status("failed", request->ota_id, "update_finalize_failed");
        s_ota_in_progress = false;
        return;
    }

    ESP_LOGI(TAG, "OTA update finished successfully");
    ESP_LOGI(TAG, "Rebooting to verify new firmware.");
    esp_restart();
}

static void ota_task(void *arg)
{
    ota_request_t *request = (ota_request_t *)arg;

    run_ota_request(request);

    s_ota_in_progress = false;
    free(request);
    vTaskDelete(NULL);
}

static void schedule_ota_request(int ota_id, int expected_size, const char *version, const char *url)
{
    ota_request_t *request = calloc(1, sizeof(*request));

    if (request == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA request");
        clear_pending_ota_record();
        publish_ota_status("failed", ota_id, "no_memory");
        return;
    }

    request->ota_id = ota_id;
    request->expected_size = expected_size;
    normalize_version_string(version, request->version, sizeof(request->version));
    snprintf(request->url, sizeof(request->url), "%s", url);

    if (xTaskCreate(ota_task,
                    "protonest_ota",
                    PROTONEST_OTA_TASK_STACK_SIZE,
                    request,
                    5,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        free(request);
        clear_pending_ota_record();
        publish_ota_status("failed", ota_id, "task_create_failed");
        return;
    }

    s_ota_in_progress = true;
}

static void handle_ota_payload_message(const char *topic, const char *payload, size_t payload_length)
{
    cJSON *root;
    const cJSON *ota_id_item;
    const cJSON *version_item;
    const cJSON *url_item;
    const cJSON *file_size_item;
    int ota_id;
    int expected_size = -1;
    const char *version = "";
    const char *url = NULL;

    ESP_LOGI(TAG, "Received OTA request payload");
    ESP_LOGI(TAG, "Validating payload");

    root = cJSON_ParseWithLength(payload, payload_length);
    if (root == NULL) {
        ESP_LOGE(TAG, "Invalid OTA payload on %s", topic);
        publish_ota_status("failed", -1, "invalid_payload");
        return;
    }

    /* Protonest OTA payloads require otaId and url. version and file_size are
       optional here; version falls back to the running app version. */
    ota_id_item = cJSON_GetObjectItemCaseSensitive(root, "otaId");
    version_item = cJSON_GetObjectItemCaseSensitive(root, "version");
    url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
    file_size_item = cJSON_GetObjectItemCaseSensitive(root, "file_size");

    if (!cJSON_IsNumber(ota_id_item) || !cJSON_IsString(url_item) || url_item->valuestring == NULL || url_item->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "OTA payload missing otaId or url");
        cJSON_Delete(root);
        publish_ota_status("failed", -1, "missing_required_fields");
        return;
    }

    ota_id = ota_id_item->valueint;
    if (ota_id < 0) {
        ESP_LOGE(TAG, "OTA payload has invalid otaId");
        cJSON_Delete(root);
        publish_ota_status("failed", ota_id, "missing_required_fields");
        return;
    }

    if (cJSON_IsString(version_item) && version_item->valuestring != NULL) {
        version = version_item->valuestring;
    }

    if (cJSON_IsNumber(file_size_item)) {
        expected_size = file_size_item->valueint;
    }

    url = url_item->valuestring;

    if (s_pending_firmware_needs_confirmation && !s_pending_firmware_confirmed) {
        ESP_LOGW(TAG, "Skipping OTA request while current firmware is pending verification");
        cJSON_Delete(root);
        publish_ota_status("rejected", ota_id, "pending_verification");
        return;
    }

    if (s_ota_in_progress) {
        ESP_LOGW(TAG, "Ignoring otaId=%d because another OTA is already running", ota_id);
        cJSON_Delete(root);
        publish_ota_status("rejected", ota_id, "ota_in_progress");
        return;
    }

    if (ota_id == s_last_handled_ota_id) {
        cJSON_Delete(root);
        return;
    }

    if (should_ignore_ota_id(ota_id)) {
        cJSON_Delete(root);
        return;
    }

    if (!ota_id_is_allowed(ota_id)) {
        cJSON_Delete(root);
        publish_ota_status("rejected", ota_id, "ota_id_not_allowed");
        return;
    }

    s_last_handled_ota_id = ota_id;

    if (!stage_pending_ota_record(ota_id, version)) {
        cJSON_Delete(root);
        publish_ota_status("failed", ota_id, "nvs_stage_failed");
        return;
    }

    schedule_ota_request(ota_id, expected_size, version, url);
    cJSON_Delete(root);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        char pending_topic[128];
        int message_id;

        build_ota_pending_topic(pending_topic, sizeof(pending_topic));
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGI(TAG, "MQTT connected");

        message_id = esp_mqtt_client_subscribe(s_mqtt_client, pending_topic, 1);
        ESP_LOGI(TAG,
                 "Subscribed to %s (%s, msg_id=%d)",
                 pending_topic,
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
        int end_offset;

        if (event->current_data_offset == 0) {
            cleanup_incoming_payload();

            if (event->topic != NULL && event->topic_len > 0) {
                size_t topic_length = (size_t)event->topic_len;
                if (topic_length >= sizeof(s_incoming_topic)) {
                    topic_length = sizeof(s_incoming_topic) - 1;
                }
                memcpy(s_incoming_topic, event->topic, topic_length);
                s_incoming_topic[topic_length] = '\0';
            }

            s_incoming_payload = calloc((size_t)event->total_data_len + 1U, sizeof(char));
            if (s_incoming_payload == NULL) {
                ESP_LOGE(TAG, "Failed to allocate buffer for OTA payload");
                cleanup_incoming_payload();
                break;
            }
            s_incoming_payload_total_len = event->total_data_len;
        }

        if (s_incoming_payload == NULL || event->current_data_offset + event->data_len > s_incoming_payload_total_len) {
            ESP_LOGE(TAG, "OTA payload assembly failed");
            cleanup_incoming_payload();
            break;
        }

        memcpy(s_incoming_payload + event->current_data_offset, event->data, (size_t)event->data_len);
        end_offset = event->current_data_offset + event->data_len;
        if (end_offset >= s_incoming_payload_total_len) {
            s_incoming_payload[s_incoming_payload_total_len] = '\0';
            handle_ota_payload_message(s_incoming_topic, s_incoming_payload, (size_t)s_incoming_payload_total_len);
            cleanup_incoming_payload();
        }
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
        .buffer = {
            .size = PROTONEST_MQTT_BUFFER_SIZE,
            .out_size = 1024,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
}

void app_main(void)
{
    configure_log_levels();

    ESP_LOGI(TAG, "Protonest Connect ESP-IDF PSK OTA example");
    ESP_LOGI(TAG, "Transport: MQTT + HTTPS");
    ESP_LOGI(TAG, "Mode: PSK");
    ESP_LOGI(TAG, "MQTT client_id == username == device name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "OTA pending topic: protonest/%s/ota/pending", DEVICE_NAME);
    ESP_LOGI(TAG, "OTA status topic: protonest/%s/ota/status/update", DEVICE_NAME);
    ESP_LOGI(TAG, "App version: %s", PROTONEST_APP_VERSION);
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

    init_rollback_verification();
    load_ota_metadata();

    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi setup failed. Check SSID, password, and AP availability.");
        ESP_LOGI(TAG, "Stopping before MQTT startup.");
        vTaskDelete(NULL);
    }

    mqtt_start();

    if (s_pending_firmware_needs_confirmation) {
        if (xTaskCreate(health_monitor_task, "protonest_health", 4096, NULL, 4, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create health monitor task");
        }
    }

    gpio_reset_pin((gpio_num_t)PROTONEST_LED_PIN);
    gpio_set_direction((gpio_num_t)PROTONEST_LED_PIN, GPIO_MODE_OUTPUT);
    if (xTaskCreate(led_blink_task, "protonest_led", 2048, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED blink task");
    }

    vTaskDelete(NULL);
}
