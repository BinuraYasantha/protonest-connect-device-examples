# x509_ota

`x509_ota` is the ESP-IDF OTA example for Protonest Connect devices using X.509 client certificate authentication.

It listens for OTA requests over MQTT, downloads the firmware binary over HTTPS, installs it into the next OTA partition, reboots, validates the new app, and cancels rollback after the post-boot health check passes.

## Files To Edit

Edit [`main/Config.h`](main/Config.h):

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define DEVICE_NAME "YOUR_DEVICE_NAME"
```

From the downloaded X.509 ZIP:

- Copy `root-ca.crt` into `main/certs/root-ca.crt`.
- Copy `http-root-ca.pem` into `main/certs/http-root-ca.pem`.
- Copy the device certificate into `main/certs/device-cert.pem`.
- Copy the device private key into `main/certs/device-key.pem`.

No MQTT username or MQTT password is needed for X.509 examples. The MQTT client ID is the device name, and the certificate/key pair authenticates the device.

The device certificate common name must match `DEVICE_NAME`.

## OTA Topics

The example subscribes to:

```text
protonest/<device>/ota/pending
```

The example publishes OTA status to:

```text
protonest/<device>/ota/status/update
```

Status payloads contain only `status` and `otaId`:

```json
{"status":"completed","otaId":"351"}
```

## Sample OTA Payload

The Protonest Connect console publishes the OTA payload for you when you send an OTA update. A sample payload looks like this:

```json
{
  "version": "1.1.0",
  "date": "2026-06-30",
  "file_size": 1189648,
  "notes": "OTA update initiated",
  "otaId": 351,
  "url": "https://api.protonestconnect.co/api/v1/ota/download/351/exampledevice?token=replace-me"
}
```

## Build And Flash

The OTA example uses ESP-IDF's built-in two-OTA-app partition configuration and rollback support from [`sdkconfig.defaults`](sdkconfig.defaults).

If this board previously ran a non-OTA example, erase flash once:

```powershell
idf.py erase-flash
```

Then build, flash, and monitor:

```powershell
idf.py set-target esp32c6
idf.py build flash monitor
```

## Test OTA

1. Build the firmware version you want to send.
2. Use the generated binary from `build/protonest_x509_ota.bin`.
3. Open the Protonest Connect console.
4. Go to `Projects > Devices > Manage Device`.
5. Click `Send OTA Update`.
6. Select or drag and drop the `.bin` file.
7. Enter an OTA version, such as `1.1.0`.
8. Click `Send`.

After you click `Send`, the console treats the update as pending. The device should then publish `completed` after the new firmware reboots and passes the health check, or `failed` if download/install fails. A rejected update publishes `rejected`, and an already-installed retained OTA request is ignored silently.

## Expected Serial Output

```text
Protonest Connect ESP-IDF X.509 OTA example
MQTT client_id == device name: exampledevice
OTA pending topic: protonest/exampledevice/ota/pending
OTA status topic: protonest/exampledevice/ota/status/update
App version: 0.0.1
Device certificate CN: exampledevice
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock...
Clock synced: 1782905517
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice
MQTT connected
Subscribed to protonest/exampledevice/ota/pending (ok, msg_id=1)
Received OTA request payload
Validating payload
Starting OTA update id=351 version=1.1.0 url=https://api.protonestconnect.co/api/v1/ota/download/351/exampledevice?token=replace-me
Expected size=1189648, remote size=1189648
Downloading firmware [................................]   0% (4096/1189648 bytes)
Downloading firmware [############....................]  40% (475136/1189648 bytes)
Downloading firmware [#########################.......]  80% (951296/1189648 bytes)
Downloading firmware [################################] 100% (1189648/1189648 bytes)
OTA update finished successfully
Rebooting to verify new firmware.
New OTA image is pending verification
Pending OTA metadata: otaId=351 version=1.1.0
Waiting for post-boot health check before confirming firmware
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock...
Clock synced: 1782905517
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice
MQTT connected
Post-boot health check passed
Firmware marked valid. Rollback cancelled.
Publish protonest/exampledevice/ota/status/update -> {"status":"completed","otaId":"351"} (queued, msg_id=2)
```

## Rollback Behavior

Rollback support is enabled by `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. After OTA, ESP-IDF boots the new image as pending verification. If Wi-Fi and MQTT become healthy within the configured timeout, the app calls `esp_ota_mark_app_valid_cancel_rollback()`. If health validation does not pass, ESP-IDF rolls back to the previous firmware.
