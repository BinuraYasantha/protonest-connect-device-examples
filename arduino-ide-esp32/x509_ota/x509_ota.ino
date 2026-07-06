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
String clientCertPem;
String clientKeyPem;
int lastHandledOtaId = -1;

struct OtaMetadataRecord {
  int otaId = -1;
  String version;

  bool isSet() const {
    return otaId >= 0 || !version.isEmpty();
  }
};

constexpr char OTA_NVS_NAMESPACE[] = "pn_ota";
constexpr char OTA_KEY_CURRENT_ID[] = "cur_id";
constexpr char OTA_KEY_CURRENT_VER[] = "cur_ver";
constexpr char OTA_KEY_PREVIOUS_ID[] = "prev_id";
constexpr char OTA_KEY_PREVIOUS_VER[] = "prev_ver";
constexpr char OTA_KEY_PENDING_ID[] = "pend_id";
constexpr char OTA_KEY_PENDING_VER[] = "pend_ver";

OtaMetadataRecord currentOtaRecord;
OtaMetadataRecord previousOtaRecord;
OtaMetadataRecord pendingOtaRecord;
bool otaStorageReady = false;
bool pendingFirmwareNeedsConfirmation = false;
bool pendingFirmwareConfirmed = false;
unsigned long pendingFirmwareStartMs = 0;
bool bootloaderRollbackPending = false;

bool publishOtaStatus(const char* status, int otaId, const char* reason = nullptr);

#ifdef CONFIG_APP_ROLLBACK_ENABLE
extern "C" bool verifyRollbackLater() {
  return true;
}
#endif

void printProgressBar(size_t current, size_t total) {
  constexpr size_t kBarWidth = 32;
  char bar[kBarWidth + 1];
  size_t filled = total > 0 ? (current * kBarWidth) / total : 0;
  if (filled > kBarWidth) {
    filled = kBarWidth;
  }

  for (size_t i = 0; i < kBarWidth; ++i) {
    bar[i] = i < filled ? '#' : '.';
  }
  bar[kBarWidth] = '\0';

  unsigned int percent = total > 0 ? static_cast<unsigned int>((current * 100) / total) : 0;
  if (percent > 100) {
    percent = 100;
  }

  Serial.printf(
      "Downloading firmware [%s] %3u%% (%u/%u bytes)\n",
      bar,
      percent,
      static_cast<unsigned int>(current),
      static_cast<unsigned int>(total));
}

String buildOtaPendingTopic() {
  return "protonest/" + String(ProtonestConfig::DEVICE_NAME) + "/ota/pending";
}

String buildOtaStatusTopic() {
  return "protonest/" + String(ProtonestConfig::DEVICE_NAME) + "/ota/status/update";
}

String normalizeVersionString(const char* version) {
  if (version && version[0] != '\0') {
    return String(version);
  }

  return String(ProtonestConfig::APP_VERSION);
}

bool openOtaStorage() {
  if (otaStorageReady) {
    return true;
  }

  if (!otaPrefs.begin(OTA_NVS_NAMESPACE, false)) {
    Serial.println("Failed to open OTA metadata storage");
    return false;
  }

  otaStorageReady = true;
  return true;
}

void loadOtaRecord(const char* idKey, const char* versionKey, OtaMetadataRecord& record) {
  record.otaId = otaPrefs.getInt(idKey, -1);
  record.version = otaPrefs.getString(versionKey, "");
}

void saveOtaRecord(const char* idKey, const char* versionKey, OtaMetadataRecord& target, const OtaMetadataRecord& value) {
  otaPrefs.putInt(idKey, value.otaId);
  otaPrefs.putString(versionKey, value.version);
  target = value;
}

void clearOtaRecord(const char* idKey, const char* versionKey, OtaMetadataRecord& target) {
  otaPrefs.remove(idKey);
  otaPrefs.remove(versionKey);
  target = OtaMetadataRecord{};
}

void clearPendingOtaRecord() {
  if (!otaStorageReady || !pendingOtaRecord.isSet()) {
    return;
  }

  clearOtaRecord(OTA_KEY_PENDING_ID, OTA_KEY_PENDING_VER, pendingOtaRecord);
}

void promotePendingOtaRecord() {
  if (!otaStorageReady || !pendingOtaRecord.isSet()) {
    return;
  }

  saveOtaRecord(OTA_KEY_PREVIOUS_ID, OTA_KEY_PREVIOUS_VER, previousOtaRecord, currentOtaRecord);

  OtaMetadataRecord confirmedRecord = pendingOtaRecord;
  if (confirmedRecord.version.isEmpty()) {
    confirmedRecord.version = ProtonestConfig::APP_VERSION;
  }

  saveOtaRecord(OTA_KEY_CURRENT_ID, OTA_KEY_CURRENT_VER, currentOtaRecord, confirmedRecord);
  clearPendingOtaRecord();
}

bool stagePendingOtaRecord(int otaId, const char* version) {
  if (!openOtaStorage()) {
    return false;
  }

  OtaMetadataRecord pendingRecord;
  pendingRecord.otaId = otaId;
  pendingRecord.version = normalizeVersionString(version);
  saveOtaRecord(OTA_KEY_PENDING_ID, OTA_KEY_PENDING_VER, pendingOtaRecord, pendingRecord);
  return true;
}

void loadOtaMetadata() {
  if (!openOtaStorage()) {
    return;
  }

  loadOtaRecord(OTA_KEY_CURRENT_ID, OTA_KEY_CURRENT_VER, currentOtaRecord);
  loadOtaRecord(OTA_KEY_PREVIOUS_ID, OTA_KEY_PREVIOUS_VER, previousOtaRecord);
  loadOtaRecord(OTA_KEY_PENDING_ID, OTA_KEY_PENDING_VER, pendingOtaRecord);

  String runningVersion = ProtonestConfig::APP_VERSION;
  bool currentMatchesRunning = currentOtaRecord.version == runningVersion;
  bool pendingMatchesRunning = pendingOtaRecord.version == runningVersion;

  if (!currentOtaRecord.isSet()) {
    currentOtaRecord.version = runningVersion;
    saveOtaRecord(OTA_KEY_CURRENT_ID, OTA_KEY_CURRENT_VER, currentOtaRecord, currentOtaRecord);
    currentMatchesRunning = true;
  }

  if (bootloaderRollbackPending || pendingMatchesRunning) {
    pendingFirmwareNeedsConfirmation = true;
    pendingFirmwareStartMs = millis();

    if (bootloaderRollbackPending) {
      Serial.println("New OTA image is pending verification");
    }

    if (pendingOtaRecord.isSet()) {
      Serial.printf(
          "Pending OTA metadata: otaId=%d version=%s\n",
          pendingOtaRecord.otaId,
          pendingOtaRecord.version.c_str());
    }

    Serial.println("Waiting for post-boot health check before confirming firmware");
    return;
  }

  if (pendingOtaRecord.isSet() && currentMatchesRunning) {
    Serial.println("Clearing stale pending OTA metadata");
    clearPendingOtaRecord();
  }

  if (!currentMatchesRunning && !pendingOtaRecord.isSet()) {
    saveOtaRecord(OTA_KEY_PREVIOUS_ID, OTA_KEY_PREVIOUS_VER, previousOtaRecord, currentOtaRecord);

    currentOtaRecord.otaId = -1;
    currentOtaRecord.version = runningVersion;
    saveOtaRecord(OTA_KEY_CURRENT_ID, OTA_KEY_CURRENT_VER, currentOtaRecord, currentOtaRecord);
  }
}

bool otaIdIsAllowed(int otaId) {
  if (pendingOtaRecord.isSet() && otaId == pendingOtaRecord.otaId) {
    Serial.printf("Rejecting otaId=%d because it is already pending verification\n", otaId);
    return false;
  }

  if (currentOtaRecord.otaId >= 0 && otaId <= currentOtaRecord.otaId) {
    Serial.printf(
        "Rejecting otaId=%d because current confirmed otaId=%d\n",
        otaId,
        currentOtaRecord.otaId);
    return false;
  }

  if (previousOtaRecord.otaId >= 0 && otaId == previousOtaRecord.otaId) {
    Serial.printf(
        "Rejecting otaId=%d because it matches previous confirmed otaId=%d\n",
        otaId,
        previousOtaRecord.otaId);
    return false;
  }

  return true;
}

bool shouldIgnoreOtaIdSilently(int otaId) {
  if (otaId == lastHandledOtaId) {
    return true;
  }

  if (pendingOtaRecord.isSet() && otaId == pendingOtaRecord.otaId) {
    return true;
  }

  if (currentOtaRecord.otaId >= 0 && otaId == currentOtaRecord.otaId) {
    return true;
  }

  if (previousOtaRecord.otaId >= 0 && otaId == previousOtaRecord.otaId) {
    return true;
  }

  return false;
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

void handleRollbackVerification() {
  if (!pendingFirmwareNeedsConfirmation || pendingFirmwareConfirmed) {
    return;
  }

  unsigned long elapsedMs = millis() - pendingFirmwareStartMs;
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  bool mqttOk = mqttClient.connected();

  if (wifiOk && mqttOk && elapsedMs >= ProtonestConfig::ROLLBACK_CONFIRM_DELAY_MS) {
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

    int confirmedOtaId = pendingOtaRecord.otaId;
    promotePendingOtaRecord();
    publishOtaStatus("completed", confirmedOtaId);
    pendingFirmwareNeedsConfirmation = false;
    pendingFirmwareConfirmed = true;
    return;
  }

#ifdef CONFIG_APP_ROLLBACK_ENABLE
  if (bootloaderRollbackPending && elapsedMs >= ProtonestConfig::ROLLBACK_VERIFY_TIMEOUT_MS) {
    Serial.println("Post-boot health check timed out");
    Serial.println("Rolling back to previous firmware");
    delay(100);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
#endif
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

  if (!readFileToString(ProtonestConfig::MQTT_ROOT_CA_PATH, mqttRootCaPem) ||
      !readFileToString(ProtonestConfig::HTTPS_ROOT_CA_PATH, httpsRootCaPem) ||
      !readFileToString(ProtonestConfig::CLIENT_CERT_PATH, clientCertPem) ||
      !readFileToString(ProtonestConfig::CLIENT_KEY_PATH, clientKeyPem)) {
    Serial.println("Certificate asset load failed");
    return false;
  }

  secureClient.setCACert(mqttRootCaPem.c_str());
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

bool publishOtaStatus(const char* status, int otaId, const char* reason) {
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["otaId"] = String(otaId);

  char buffer[256];
  serializeJson(doc, buffer, sizeof(buffer));

  String statusTopic = buildOtaStatusTopic();
  bool ok = mqttClient.publish(statusTopic.c_str(), buffer);
  if (!ok) {
    Serial.printf("Failed to publish OTA status to %s\n", statusTopic.c_str());
    return false;
  }

  Serial.printf("Published OTA status %s -> %s\n", statusTopic.c_str(), buffer);
  return true;
}

bool connectMqtt() {
  if (!mqttClient.connect(ProtonestConfig::MQTT_CLIENT_ID)) {
    return false;
  }

  String pendingTopic = buildOtaPendingTopic();
  bool ok = mqttClient.subscribe(pendingTopic.c_str(), 1);
  Serial.printf("Subscribed to %s (%s)\n", pendingTopic.c_str(), ok ? "ok" : "failed");
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
      if (millis() - lastByteAt > 15000) {
        if (printedProgress) {
          Serial.println();
        }
        Serial.println("\nDownload stalled");
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
      if (printedProgress) {
        Serial.println();
      }
      Serial.printf("\nUpdate.write failed. Error=%u\n", Update.getError());
      return false;
    }

    totalWritten += bytesWritten;

    if (contentLength > 0) {
      int percent = static_cast<int>((totalWritten * 100) / static_cast<size_t>(contentLength));
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

  if (contentLength > 0) {
    printProgressBar(static_cast<size_t>(contentLength), static_cast<size_t>(contentLength));
    printedProgress = true;
  }

  if (printedProgress) {
    Serial.println();
  }
  Serial.printf("OTA bytes written: %u\n", static_cast<unsigned int>(totalWritten));

  return contentLength < 0 || totalWritten == static_cast<size_t>(contentLength);
}

void runOta(const String& url, int otaId, const char* version, int expectedSize) {
  WiFiClientSecure otaClient;
  otaClient.setCACert(httpsRootCaPem.c_str());
  otaClient.setTimeout(12000);

  Serial.printf("Starting OTA update id=%d version=%s\n", otaId, version);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(false);
  http.setUserAgent("Protonest-ESP32-OTA");

  if (!http.begin(otaClient, url)) {
    Serial.println("HTTP begin failed for OTA");
    clearPendingOtaRecord();
    publishOtaStatus("failed", otaId, "http_begin_failed");
    return;
  }

  http.addHeader("Accept", "application/octet-stream");

  int responseCode = http.GET();
  Serial.printf("OTA HTTP status: %d\n", responseCode);
  if (responseCode != HTTP_CODE_OK) {
    http.end();
    clearPendingOtaRecord();
    publishOtaStatus("failed", otaId, "http_status_not_ok");
    return;
  }

  int contentLength = http.getSize();
  Serial.printf("OTA content-length: %d\n", contentLength);

  if (expectedSize > 0 && contentLength > 0 && contentLength != expectedSize) {
    Serial.printf("Expected file_size=%d but server returned %d\n", expectedSize, contentLength);
    http.end();
    clearPendingOtaRecord();
    publishOtaStatus("failed", otaId, "size_mismatch");
    return;
  }

  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    Serial.printf("Update.begin failed. Error=%u\n", Update.getError());
    http.end();
    clearPendingOtaRecord();
    publishOtaStatus("failed", otaId, "update_begin_failed");
    return;
  }

  if (!downloadFirmwareToFlash(http, contentLength)) {
    Update.abort();
    http.end();
    clearPendingOtaRecord();
    publishOtaStatus("failed", otaId, "download_failed");
    return;
  }

  if (!Update.end()) {
    Serial.printf("Update.end failed. Error=%u\n", Update.getError());
    http.end();
    clearPendingOtaRecord();
    publishOtaStatus("failed", otaId, "update_finalize_failed");
    return;
  }

  http.end();

  if (!Update.isFinished()) {
    Serial.println("Update not finished after download");
    clearPendingOtaRecord();
    publishOtaStatus("failed", otaId, "update_not_finished");
    return;
  }

  Serial.println("OTA update finished successfully");
  Serial.println("Device rebooting.");
  ESP.restart();
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("Invalid OTA payload on %s: %s\n", topic, error.c_str());
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

  if (shouldIgnoreOtaIdSilently(otaId)) {
    return;
  }

  if (!otaIdIsAllowed(otaId)) {
    publishOtaStatus("rejected", otaId, "ota_id_not_allowed");
    return;
  }

  lastHandledOtaId = otaId;

  Serial.println("Received OTA request payload");
  if (!stagePendingOtaRecord(otaId, version)) {
    publishOtaStatus("failed", otaId, "nvs_stage_failed");
    return;
  }
  runOta(String(url), otaId, version, expectedSize);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  Serial.println("Rollback support: enabled");
#else
  Serial.println("Rollback support: disabled");
#endif
  initRollbackVerification();
  loadOtaMetadata();

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
  handleRollbackVerification();
}

void loop() {
  ensureConnections();
  mqttClient.loop();
  handleRollbackVerification();
}
