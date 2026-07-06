#include <Arduino.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "Config.h"

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

String rootCaPem;
unsigned long lastPublishMs = 0;

String buildStreamTopic(const char* streamName) {
  return "protonest/" + String(ProtonestConfig::DEVICE_NAME) + "/stream/" + String(streamName);
}

bool readFileToString(const char* path, String& output) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.printf("Failed to open %s\n", path);
    return false;
  }

  output = file.readString();
  file.close();
  output.trim();
  return !output.isEmpty();
}

bool loadTlsAssets() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return false;
  }

  if (!readFileToString(ProtonestConfig::ROOT_CA_PATH, rootCaPem)) {
    Serial.println("Root CA load failed");
    return false;
  }

  secureClient.setCACert(rootCaPem.c_str());
  return true;
}

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ProtonestConfig::WIFI_SSID, ProtonestConfig::WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startMs > ProtonestConfig::WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("\nWi-Fi connect timed out");
      return false;
    }

    delay(500);
    Serial.print(".");
  }

  Serial.printf("\nWi-Fi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

bool syncClock() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing clock");

  for (uint8_t attempt = 0; attempt < 40; ++attempt) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      Serial.printf("\nClock synced: %lld\n", static_cast<long long>(now));
      return true;
    }

    delay(500);
    Serial.print(".");
  }

  Serial.println("\nClock sync failed");
  return false;
}

bool connectMqtt() {
  return mqttClient.connect(
      ProtonestConfig::MQTT_CLIENT_ID,
      ProtonestConfig::MQTT_USERNAME,
      ProtonestConfig::MQTT_PASSWORD);
}

void ensureConnections() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  if (!mqttClient.connected()) {
    Serial.printf("Connecting to MQTT broker %s:%u\n", ProtonestConfig::MQTT_HOST, ProtonestConfig::MQTT_PORT);
    if (connectMqtt()) {
      Serial.println("MQTT connected");
      return;
    }

    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
    delay(ProtonestConfig::MQTT_RECONNECT_DELAY_MS);
  }
}

void publishSample() {
  char payload[128];
  float sampleTemperature = 23.5f + static_cast<float>((millis() / 1000UL) % 20UL) / 10.0f;
  snprintf(
      payload,
      sizeof(payload),
      "{\"temperature_c\":%.1f,\"uptime_ms\":%lu}",
      sampleTemperature,
      millis());

  String topic = buildStreamTopic(ProtonestConfig::STREAM_NAME);
  bool ok = mqttClient.publish(topic.c_str(), payload);
  Serial.printf("Publish %s -> %s (%s)\n", topic.c_str(), payload, ok ? "ok" : "failed");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!connectWifi()) {
    return;
  }

  if (!syncClock()) {
    return;
  }

  if (!loadTlsAssets()) {
    return;
  }

  mqttClient.setServer(ProtonestConfig::MQTT_HOST, ProtonestConfig::MQTT_PORT);
  ensureConnections();
}

void loop() {
  ensureConnections();
  mqttClient.loop();

  if (mqttClient.connected() && millis() - lastPublishMs >= ProtonestConfig::PUBLISH_INTERVAL_MS) {
    lastPublishMs = millis();
    publishSample();
  }
}

