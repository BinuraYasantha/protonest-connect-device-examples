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
String clientCertPem;
String clientKeyPem;
unsigned long lastStatusPrintMs = 0;

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

  if (!readFileToString(ProtonestConfig::ROOT_CA_PATH, rootCaPem) ||
      !readFileToString(ProtonestConfig::CLIENT_CERT_PATH, clientCertPem) ||
      !readFileToString(ProtonestConfig::CLIENT_KEY_PATH, clientKeyPem)) {
    Serial.println("Certificate asset load failed");
    return false;
  }

  secureClient.setCACert(rootCaPem.c_str());
  secureClient.setCertificate(clientCertPem.c_str());
  secureClient.setPrivateKey(clientKeyPem.c_str());
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
  Serial.printf(
      "Connecting to MQTT broker %s:%u\n",
      ProtonestConfig::MQTT_HOST,
      ProtonestConfig::MQTT_PORT);

  return mqttClient.connect(ProtonestConfig::MQTT_CLIENT_ID);
}

void ensureConnections() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  if (!mqttClient.connected()) {
    if (connectMqtt()) {
      Serial.println("MQTT connected");
      return;
    }

    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
    delay(ProtonestConfig::MQTT_RECONNECT_DELAY_MS);
  }
}

void printStatus() {
  Serial.printf(
      "Status: wifi=%s mqtt=%s rssi=%d device=%s\n",
      WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
      mqttClient.connected() ? "connected" : "disconnected",
      WiFi.RSSI(),
      ProtonestConfig::DEVICE_NAME);
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

  if (millis() - lastStatusPrintMs >= ProtonestConfig::STATUS_PRINT_INTERVAL_MS) {
    lastStatusPrintMs = millis();
    printStatus();
  }
}
