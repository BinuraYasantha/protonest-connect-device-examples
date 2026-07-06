#pragma once

#include <stdint.h>

namespace ProtonestConfig {

constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

constexpr char DEVICE_NAME[] = "YOUR_DEVICE_NAME";
constexpr const char* MQTT_CLIENT_ID = DEVICE_NAME;
constexpr const char* MQTT_USERNAME = DEVICE_NAME;
constexpr char MQTT_PASSWORD[] = "YOUR_MQTT_PASSWORD";

constexpr char MQTT_HOST[] = "mqtt.protonest.co";
constexpr uint16_t MQTT_PORT = 8883;

constexpr char ROOT_CA_PATH[] = "/root-ca.crt";

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t MQTT_RECONNECT_DELAY_MS = 5000;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 30000;

}  // namespace ProtonestConfig
