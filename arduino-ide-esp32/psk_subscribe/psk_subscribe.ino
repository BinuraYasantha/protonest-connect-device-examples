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

String buildTopic(const char* suffix) {
  return "protonest/" + String(ProtonestConfig::DEVICE_NAME) + "/" + String(suffix);
}

String buildStateTopic(const char* stateName) {
  return "protonest/" + String(ProtonestConfig::DEVICE_NAME) + "/state/" + String(stateName);
}

String buildCommandTopic() {
  return buildTopic(ProtonestConfig::SUBSCRIBE_TOPIC_SUFFIX);
}

bool isCommandTopic(const char* topic) {
  String expectedTopic = buildCommandTopic();
  return strcmp(topic, expectedTopic.c_str()) == 0;
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

void onMessage(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int index = 0; index < length; ++index) {
    message += static_cast<char>(payload[index]);
  }

  Serial.printf("Incoming %s -> %s\n", topic, message.c_str());

  if (!isCommandTopic(topic)) {
    Serial.printf("Message received on non-command topic, expected %s, skipping ack\n", buildCommandTopic().c_str());
    return;
  }

  String ackTopic = buildStateTopic(ProtonestConfig::ACK_STATE_NAME);
  bool ok = mqttClient.publish(ackTopic.c_str(), message.c_str());
  Serial.printf("Ack %s (%s)\n", ackTopic.c_str(), ok ? "ok" : "failed");
}

bool connectMqtt() {
  if (!mqttClient.connect(
          ProtonestConfig::MQTT_CLIENT_ID,
          ProtonestConfig::MQTT_USERNAME,
          ProtonestConfig::MQTT_PASSWORD)) {
    return false;
  }

  String subscribeTopic = buildCommandTopic();
  bool ok = mqttClient.subscribe(subscribeTopic.c_str(), 1);
  Serial.printf("Subscribed to %s (%s)\n", subscribeTopic.c_str(), ok ? "ok" : "failed");
  return ok;
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
  mqttClient.setCallback(onMessage);
  ensureConnections();
}

void loop() {
  ensureConnections();
  mqttClient.loop();
}
