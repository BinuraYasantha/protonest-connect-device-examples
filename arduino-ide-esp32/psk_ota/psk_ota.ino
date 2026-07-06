#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <sdkconfig.h>
#include <time.h>

#ifdef CONFIG_APP_ROLLBACK_ENABLE
#include <esp_err.h>
#include <esp_ota_ops.h>
#endif

#include "Config.h"

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
Preferences otaPrefs;

String mqttRootCaPem;
String httpsRootCaPem;
unsigned long lastStatusPrintMs = 0;
int lastHandledOtaId = -1;
int confirmedOtaId = -1;
int pendingOtaId = -1;
String pendingOtaVersion;
bool otaStorageReady = false;
bool bootloaderRollbackPending = false;
bool pendingFirmwareNeedsConfirmation = false;
bool pendingFirmwareConfirmed = false;
unsigned long pendingFirmwareStartMs = 0;

constexpr char OTA_NAMESPACE[] = "pn_ota_new";
constexpr char OTA_KEY_CONFIRMED_ID[] = "ok_id";
constexpr char OTA_KEY_PENDING_ID[] = "pend_id";
constexpr char OTA_KEY_PENDING_VER[] = "pend_ver";

#ifdef CONFIG_APP_ROLLBACK_ENABLE
extern "C" bool verifyRollbackLater() {
  return true;
}
#endif

String buildOtaPendingTopic() {
  return "protonest/" + String(ProtonestConfig::DEVICE_NAME) + "/ota/pending";
}

String buildOtaStatusTopic() {
  return "protonest/" + String(ProtonestConfig::DEVICE_NAME) + "/ota/status/update";
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
  Serial.println("Mounting LittleFS and loading TLS assets");
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return false;
  }

  if (!readFileToString(ProtonestConfig::MQTT_ROOT_CA_PATH, mqttRootCaPem)) {
    Serial.println("MQTT root CA load failed");
    return false;
  }

  if (!readFileToString(ProtonestConfig::HTTPS_ROOT_CA_PATH, httpsRootCaPem)) {
    Serial.println("HTTPS root CA load failed");
    return false;
  }

  secureClient.setCACert(mqttRootCaPem.c_str());
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

bool openOtaStorage() {
  if (otaStorageReady) {
    return true;
  }

  if (!otaPrefs.begin(OTA_NAMESPACE, false)) {
    Serial.println("Failed to open OTA metadata storage");
    return false;
  }

  otaStorageReady = true;
  return true;
}

void clearPendingOtaRecord() {
  if (!otaStorageReady) {
    return;
  }

  otaPrefs.remove(OTA_KEY_PENDING_ID);
  otaPrefs.remove(OTA_KEY_PENDING_VER);
  pendingOtaId = -1;
  pendingOtaVersion = "";
}

bool stagePendingOtaRecord(int otaId, const char* version) {
  if (!openOtaStorage()) {
    return false;
  }

  otaPrefs.putInt(OTA_KEY_PENDING_ID, otaId);
  otaPrefs.putString(OTA_KEY_PENDING_VER, version != nullptr ? version : "");
  pendingOtaId = otaId;
  pendingOtaVersion = version != nullptr ? version : "";
  return true;
}

void saveConfirmedOtaId(int otaId) {
  if (!openOtaStorage()) {
    return;
  }

  otaPrefs.putInt(OTA_KEY_CONFIRMED_ID, otaId);
  confirmedOtaId = otaId;
}

#ifdef CONFIG_APP_ROLLBACK_ENABLE
void initRollbackVerification() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    return;
  }

  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) != ESP_OK) {
    return;
  }

  if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
    bootloaderRollbackPending = true;
  }
}
#else
void initRollbackVerification() {}
#endif

void loadOtaState() {
  if (!openOtaStorage()) {
    return;
  }

  confirmedOtaId = otaPrefs.getInt(OTA_KEY_CONFIRMED_ID, -1);
  pendingOtaId = otaPrefs.getInt(OTA_KEY_PENDING_ID, -1);
  pendingOtaVersion = otaPrefs.getString(OTA_KEY_PENDING_VER, "");

  bool runningPendingImage = pendingOtaId >= 0 && pendingOtaVersion == ProtonestConfig::APP_VERSION;

  if (bootloaderRollbackPending || runningPendingImage) {
    pendingFirmwareNeedsConfirmation = true;
    pendingFirmwareStartMs = millis();

    if (bootloaderRollbackPending) {
      Serial.println("New OTA image is pending bootloader verification");
    }

    Serial.printf(
        "Pending OTA metadata: otaId=%d version=%s\n",
        pendingOtaId,
        pendingOtaVersion.c_str());
    Serial.println("Waiting for post-boot health check before confirming firmware");
    return;
  }

  if (pendingOtaId >= 0) {
    Serial.println("Clearing stale pending OTA metadata");
    clearPendingOtaRecord();
  }
}

bool publishOtaStatus(const char* status, int otaId, const char* reason = nullptr) {
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["otaId"] = String(otaId);
  if (reason != nullptr && reason[0] != '\0') {
    Serial.printf("OTA status reason: %s\n", reason);
  }

  char buffer[256];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));
  String topic = buildOtaStatusTopic();
  bool ok = mqttClient.publish(topic.c_str(), reinterpret_cast<const uint8_t*>(buffer), len, false);
  if (!ok) {
    Serial.printf("Failed to publish OTA status to %s\n", topic.c_str());
    return false;
  }

  Serial.printf("Published OTA status %s -> %s\n", topic.c_str(), buffer);
  return true;
}

bool connectMqtt() {
  Serial.printf(
      "Connecting to MQTT broker %s:%u as %s\n",
      ProtonestConfig::MQTT_HOST,
      ProtonestConfig::MQTT_PORT,
      ProtonestConfig::MQTT_USERNAME);

  if (!mqttClient.connect(
          ProtonestConfig::MQTT_CLIENT_ID,
          ProtonestConfig::MQTT_USERNAME,
          ProtonestConfig::MQTT_PASSWORD)) {
    return false;
  }

  String pendingTopic = buildOtaPendingTopic();
  bool subscribed = mqttClient.subscribe(pendingTopic.c_str(), 1);
  Serial.printf("Subscribed to %s (%s)\n", pendingTopic.c_str(), subscribed ? "ok" : "failed");
  return subscribed;
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
      "Status: wifi=%s mqtt=%s rssi=%d device=%s confirmedOtaId=%d pendingOtaId=%d\n",
      WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
      mqttClient.connected() ? "connected" : "disconnected",
      WiFi.RSSI(),
      ProtonestConfig::DEVICE_NAME,
      confirmedOtaId,
      pendingOtaId);
}

void printProgressBar(size_t current, size_t total) {
  constexpr size_t kBarWidth = 32;
  char bar[kBarWidth + 1];
  size_t filled = total > 0 ? (current * kBarWidth) / total : 0;
  if (filled > kBarWidth) {
    filled = kBarWidth;
  }

  for (size_t index = 0; index < kBarWidth; ++index) {
    bar[index] = index < filled ? '#' : '.';
  }
  bar[kBarWidth] = '\0';

  unsigned int percent = total > 0 ? static_cast<unsigned int>((current * 100U) / total) : 0U;
  if (percent > 100U) {
    percent = 100U;
  }

  Serial.printf(
      "Downloading firmware [%s] %3u%% (%u/%u bytes)\n",
      bar,
      percent,
      static_cast<unsigned int>(current),
      static_cast<unsigned int>(total));
}

bool downloadFirmwareToFlash(HTTPClient& http, int contentLength) {
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[4096];
  size_t totalWritten = 0;
  int lastPercent = -1;
  unsigned long lastByteAt = millis();
  bool printedProgress = false;

  while (http.connected() && (contentLength < 0 || totalWritten < static_cast<size_t>(contentLength))) {
    size_t availableBytes = stream->available();
    if (availableBytes == 0) {
      if (millis() - lastByteAt > ProtonestConfig::OTA_DOWNLOAD_STALL_TIMEOUT_MS) {
        Serial.println("Download stalled");
        return false;
      }
      delay(1);
      continue;
    }

    if (availableBytes > sizeof(buffer)) {
      availableBytes = sizeof(buffer);
    }

    int bytesRead = stream->readBytes(buffer, availableBytes);
    if (bytesRead <= 0) {
      continue;
    }

    lastByteAt = millis();

    size_t bytesWritten = Update.write(buffer, static_cast<size_t>(bytesRead));
    if (bytesWritten != static_cast<size_t>(bytesRead)) {
      Serial.printf("Update.write failed. Error=%u\n", Update.getError());
      return false;
    }

    totalWritten += bytesWritten;

    if (contentLength > 0) {
      int percent = static_cast<int>((totalWritten * 100U) / static_cast<size_t>(contentLength));
      if (percent != lastPercent) {
        printProgressBar(totalWritten, static_cast<size_t>(contentLength));
        printedProgress = true;
        lastPercent = percent;
      }
    } else {
      Serial.printf("Downloaded %u bytes\n", static_cast<unsigned int>(totalWritten));
      printedProgress = true;
    }
  }

  if (contentLength > 0 && printedProgress) {
    printProgressBar(static_cast<size_t>(contentLength), static_cast<size_t>(contentLength));
  }

  Serial.printf("OTA bytes written: %u\n", static_cast<unsigned int>(totalWritten));
  return contentLength < 0 || totalWritten == static_cast<size_t>(contentLength);
}

void failOtaAndClearPending(int otaId, const char* reason) {
  clearPendingOtaRecord();
  publishOtaStatus("failed", otaId, reason);
}

void runOta(const String& url, int otaId, const char* version, int expectedSize) {
  WiFiClientSecure otaClient;
  otaClient.setCACert(httpsRootCaPem.c_str());
  otaClient.setTimeout(ProtonestConfig::OTA_HTTP_TIMEOUT_MS);

  Serial.printf("Starting OTA update id=%d version=%s\n", otaId, version != nullptr ? version : "");
  Serial.printf("OTA URL: %s\n", url.c_str());

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(false);
  http.setUserAgent("Protonest-ESP32-OTA");

  if (!http.begin(otaClient, url)) {
    Serial.println("HTTP begin failed for OTA");
    failOtaAndClearPending(otaId, "http_begin_failed");
    return;
  }

  http.addHeader("Accept", "application/octet-stream");

  int responseCode = http.GET();
  Serial.printf("OTA HTTP status: %d\n", responseCode);
  if (responseCode != HTTP_CODE_OK) {
    http.end();
    failOtaAndClearPending(otaId, "http_status_not_ok");
    return;
  }

  int contentLength = http.getSize();
  Serial.printf("OTA content-length: %d\n", contentLength);
  if (expectedSize > 0 && contentLength > 0 && expectedSize != contentLength) {
    Serial.printf("Expected file_size=%d but server returned %d\n", expectedSize, contentLength);
    http.end();
    failOtaAndClearPending(otaId, "size_mismatch");
    return;
  }

  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    Serial.printf("Update.begin failed. Error=%u\n", Update.getError());
    http.end();
    failOtaAndClearPending(otaId, "update_begin_failed");
    return;
  }

  if (!downloadFirmwareToFlash(http, contentLength)) {
    Update.abort();
    http.end();
    failOtaAndClearPending(otaId, "download_failed");
    return;
  }

  if (!Update.end()) {
    Serial.printf("Update.end failed. Error=%u\n", Update.getError());
    http.end();
    failOtaAndClearPending(otaId, "update_finalize_failed");
    return;
  }

  http.end();

  if (!Update.isFinished()) {
    Serial.println("Update not finished after download");
    failOtaAndClearPending(otaId, "update_not_finished");
    return;
  }

  Serial.println("OTA image written successfully");
  Serial.println("Rebooting into the new firmware");
  delay(300);
  ESP.restart();
}

void handlePostBootConfirmation() {
  if (!pendingFirmwareNeedsConfirmation || pendingFirmwareConfirmed) {
    return;
  }

  unsigned long elapsedMs = millis() - pendingFirmwareStartMs;
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  bool mqttOk = mqttClient.connected();

  if (wifiOk && mqttOk && elapsedMs >= ProtonestConfig::POST_BOOT_CONFIRM_DELAY_MS) {
    Serial.println("Post-boot health check passed");

#ifdef CONFIG_APP_ROLLBACK_ENABLE
    if (bootloaderRollbackPending) {
      esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
      if (err != ESP_OK) {
        Serial.printf("Failed to confirm firmware: %s\n", esp_err_to_name(err));
        return;
      }

      Serial.println("Firmware marked valid. Rollback cancelled.");
    }
#endif

    saveConfirmedOtaId(pendingOtaId);
    publishOtaStatus("completed", pendingOtaId);
    clearPendingOtaRecord();
    pendingFirmwareNeedsConfirmation = false;
    pendingFirmwareConfirmed = true;
    return;
  }

#ifdef CONFIG_APP_ROLLBACK_ENABLE
  if (bootloaderRollbackPending && elapsedMs >= ProtonestConfig::POST_BOOT_VERIFY_TIMEOUT_MS) {
    Serial.println("Post-boot health check timed out");
    Serial.println("Rolling back to the previous firmware");
    delay(100);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
#endif
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("Invalid OTA payload: %s\n", error.c_str());
    publishOtaStatus("failed", -1, "invalid_payload");
    return;
  }

  // Protonest OTA payloads require otaId, version, and url. file_size is optional
  // and is used as a download-length guard when it is present.
  int otaId = doc["otaId"] | -1;
  const char* version = doc["version"] | "";
  const char* url = doc["url"] | "";
  int expectedSize = doc["file_size"] | -1;

  if (otaId < 0 || version[0] == '\0' || url[0] == '\0') {
    Serial.println("OTA payload missing otaId, version, or url");
    publishOtaStatus("failed", otaId, "missing_required_fields");
    return;
  }

  if (otaId == lastHandledOtaId || otaId == pendingOtaId || otaId == confirmedOtaId) {
    return;
  }

  if (pendingFirmwareNeedsConfirmation) {
    Serial.println("Skipping OTA because current firmware is still pending confirmation");
    publishOtaStatus("rejected", otaId, "pending_verification");
    return;
  }

  if (confirmedOtaId >= 0 && otaId < confirmedOtaId) {
    Serial.printf("Rejecting otaId=%d because confirmed otaId=%d is newer\n", otaId, confirmedOtaId);
    publishOtaStatus("rejected", otaId, "ota_id_not_newer");
    return;
  }

  if (!stagePendingOtaRecord(otaId, version)) {
    publishOtaStatus("failed", otaId, "nvs_stage_failed");
    return;
  }

  Serial.println("Received OTA request payload");
  Serial.printf("Topic: %s\n", topic);

  lastHandledOtaId = otaId;
  runOta(String(url), otaId, version, expectedSize);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("==================================================");
  Serial.println("Protonest Connect Arduino OTA Example");
  Serial.println("Transport: MQTT + HTTPS");
  Serial.println("Mode: PSK");
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  Serial.println("Rollback support: enabled");
#else
  Serial.println("Rollback support: disabled");
#endif
  Serial.printf("MQTT client_id == username == device name: %s\n", ProtonestConfig::DEVICE_NAME);
  Serial.printf("App version: %s\n", ProtonestConfig::APP_VERSION);
  Serial.println("==================================================");

  initRollbackVerification();
  loadOtaState();

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
  mqttClient.setBufferSize(1024);
  ensureConnections();
  handlePostBootConfirmation();
}

void loop() {
  ensureConnections();
  mqttClient.loop();
  handlePostBootConfirmation();

  if (millis() - lastStatusPrintMs >= ProtonestConfig::STATUS_PRINT_INTERVAL_MS) {
    lastStatusPrintMs = millis();
    printStatus();
  }
}
