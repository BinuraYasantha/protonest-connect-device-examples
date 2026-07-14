# `x509_ota`

`x509_ota` is the Protonest OTA example for Arduino-compatible ESP32 boards that authenticate with client certificates. Use it after `x509_connect` is already working and after you are comfortable uploading normal sketches to the device.

## What this example does

This sketch:

- connects to Protonest Connect with X.509 authentication
- subscribes to `protonest/<device>/ota/pending`
- validates the OTA payload
- downloads the firmware from the URL inside the payload
- flashes the new image
- stores OTA metadata in NVS
- publishes OTA status to `protonest/<device>/ota/status/update`

For this X.509 example, the identity rule is:

- X.509 auth does not use MQTT username/password
- the configured device name is used as the MQTT client ID

The sketch uses the supplied download URL directly. It intentionally avoids a separate `HEAD` request because signed download URLs may be single-use or method-sensitive.

## Code-specific OTA behavior

This sketch implements the OTA flow in four stages:

1. Receive an MQTT payload on `protonest/<device>/ota/pending`.
2. Store the incoming `otaId` and `version` as a pending record in NVS.
3. Download and flash the binary from the HTTPS `url` in the payload.
4. Reboot, reconnect to Wi-Fi and MQTT, then confirm the firmware after the post-boot health check passes.

The sketch does not publish `completed` before reboot. It only publishes `completed` after the new firmware boots successfully and is confirmed.

## OTA topics and status payload

This example uses:

- pending topic: `protonest/<device>/ota/pending`
- status topic: `protonest/<device>/ota/status/update`

The status payload contains only two fields:

```json
{"status":"completed","otaId":"33"}
```

Status values used by this sketch:

- `completed` when the new firmware boots, passes health checks, and is confirmed
- `failed` when the OTA payload is invalid or the download / flash process fails
- `rejected` when the OTA request is well-formed but not allowed by the OTA ID rules, except for OTA IDs the sketch already knows and ignores silently

The `otaId` is published as a string.

## Files in this folder

- [x509_ota.ino](../x509_ota.ino)
- [Config.h](../Config.h)
- `data/` for LittleFS files
- [docs/README.md](README.md)

## What to edit

Edit [Config.h](../Config.h) and replace the placeholders with your own values:

- Wi-Fi SSID
- Wi-Fi password
- Protonest device name
- client certificate path
- client private key path
- application version if you want to report your own version string

## LittleFS files required

This example reads all MQTT, HTTPS, and X.509 TLS assets from LittleFS.

When you download X.509 credentials from Protonest Connect, the ZIP file contains:

```text
example_x509.zip
├── root-ca.crt
├── <device>-cert.pem
├── <device>-key.pem
└── http-root-ca.pem
```

This example already includes these CA files:

- `root-ca.crt`
- `http-root-ca.pem`

You do not need to copy these CA files from the credential ZIP, but the same files are also visible inside the ZIP downloaded from the Protonest Connect console.

Copy these files from the downloaded Protonest credential ZIP into `data/` before uploading LittleFS:

- the client certificate file from the ZIP
- the client private key file from the ZIP

Then set these paths in [Config.h](../Config.h) using the actual filenames you copied into `data/`:

```cpp
constexpr char CLIENT_CERT_PATH[] = "/<device>-cert.pem";
constexpr char CLIENT_KEY_PATH[] = "/<device>-key.pem";
```

The sketch uses:

- `root-ca.crt` for `mqtt.protonest.co`
- `http-root-ca.pem` for HTTPS OTA downloads from `https://api.protonestconnect.co/api/v1/ota/download/<otaId>/<device>?token=<token>`
- the configured client certificate and private key for MQTT client authentication

Recommended partition scheme:

- `Arduino IDE > Tools > Partition Scheme > Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)`

Why this matters for OTA:

- it provides two OTA app slots
- each firmware image can be up to about 1.9 MB
- the 128 KB filesystem is enough for the small certificate files used by this example

Limitations:

- if the compiled firmware grows beyond a single 1.9 MB app slot, OTA update will no longer fit
- the filesystem is only 128 KB, so do not treat this layout as general file storage

## Code-specific rollback and OTA ID rules

This sketch uses both ESP32 rollback support and NVS-based OTA tracking.

NVS records used by the sketch:

- `current`: last confirmed OTA ID and version
- `previous`: previous confirmed OTA ID and version
- `pending`: OTA ID and version waiting for post-boot confirmation

Post-boot timing from `Config.h`:

- `ROLLBACK_CONFIRM_DELAY_MS = 5000`
- `ROLLBACK_VERIFY_TIMEOUT_MS = 60000`

The sketch confirms the new image only after:

- Wi-Fi is connected
- MQTT is connected
- at least 5 seconds have passed since booting the new firmware

If rollback support is enabled in the installed Arduino ESP32 core and the post-boot health check never passes within 60 seconds, the sketch requests rollback to the previous firmware.

Incoming OTA requests are rejected when:

- the same `otaId` is already pending verification
- the new `otaId` is less than or equal to the current confirmed `otaId`
- the new `otaId` matches the previous confirmed `otaId`

Silent ignore cases:

- if the same retained OTA message is delivered again during the same runtime and matches `lastHandledOtaId`, the sketch ignores it quietly without publishing a new status
- if the broker re-delivers a retained OTA payload after reboot and the `otaId` matches the pending, current, or previous stored OTA record, the sketch ignores it quietly without publishing a new status

## Export firmware binary

To create the firmware file for Protonest OTA:

1. Open [x509_ota.ino](../x509_ota.ino) in Arduino IDE.
2. Make sure the correct board and partition scheme are selected.
3. Use `Sketch > Export Compiled Binary`.
4. Wait for the build to finish.
5. Check the sketch folder for the generated `.bin` files.

For Protonest OTA, use the main application binary:

- `x509_ota.ino.bin`

Do not upload these files for Protonest OTA:

- `x509_ota.ino.bootloader.bin`
- `x509_ota.ino.partitions.bin`

## Upload steps

1. Open [x509_ota.ino](../x509_ota.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Confirm `data/root-ca.crt` and `data/http-root-ca.pem` are present, then copy the client certificate and private key from the ZIP into `data/`.
4. Select the correct ESP32 board.
5. Select `Arduino IDE > Tools > Partition Scheme > Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)`.
6. Select the correct serial port.
7. Close Serial Monitor.
8. Upload `data/` to LittleFS.
9. Upload the sketch.

If you need help installing the Arduino IDE 2 LittleFS uploader and using it, follow this guide:

- [Arduino IDE 2: Install ESP32 LittleFS Uploader](https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/)

## Test OTA

To send an OTA update from the Protonest Console:

1. Open `https://console.protonestconnect.co/`
2. Go to `Projects`
3. Click `Devices`
4. Find your device
5. Click `Manage Device`
6. Click the `Send OTA Update` button
7. Select or drag and drop the exported binary file:
   `build/esp32.esp32.esp32c6/x509_ota.ino.bin`
8. Enter an OTA version
9. Click `Send`

The platform will publish the OTA payload to:

- `protonest/<device>/ota/pending`

The device will receive that payload and start the OTA workflow.

Status flow in Protonest Console:

- after you click `Send OTA Update`, the console should show `pending`
- after the new firmware boots and passes post-boot verification, the console should show `completed`
- if the download, flash, or validation process fails, the console should show `failed`
- if the OTA request is not allowed by the OTA ID rules, the console should show `rejected`
- if the broker later re-delivers the same retained payload for an already known OTA ID, the device ignores it and the console state should remain unchanged

## Sample OTA payload

```json
{
  "version": "1.1.0",
  "date": "2026-06-30",
  "file_size": 173,
  "notes": "OTA update initiated",
  "otaId": 351,
  "url": "https://api.protonestconnect.co/api/v1/ota/download/351/exampledevice?token=example-token"
}
```

## Expected serial output

Serial Monitor should show output similar to this:

```text
Rollback support: enabled
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock......
Clock synced: 1782972961
Mounting LittleFS and loading TLS assets
Connecting to MQTT broker mqtt.protonest.co:8883
Subscribed to protonest/exampledevice/ota/pending (ok)
MQTT connected
Received OTA request payload
Starting OTA update id=357 version=1.1.0
OTA HTTP status: 200
OTA content-length: 1189648
Downloading firmware [................................]   0% (4096/1189648 bytes)
Downloading firmware [########........................]  25% (297412/1189648 bytes)
Downloading firmware [################................]  50% (594824/1189648 bytes)
Downloading firmware [########################........]  75% (892236/1189648 bytes)
...
Downloading firmware [################################] 100% (1189648/1189648 bytes)

OTA bytes written: 1189648
OTA update finished successfully
Device rebooting.
New OTA image is pending verification
Pending OTA metadata: otaId=357 version=1.1.0
Waiting for post-boot health check before confirming firmware
Connecting to Wi-Fi.....
Wi-Fi connected. IP: 192.168.1.26
Syncing clock
Clock synced: 1782973032
Mounting LittleFS and loading TLS assets
Connecting to MQTT broker mqtt.protonest.co:8883
Subscribed to protonest/exampledevice/ota/pending (ok)
MQTT connected
Post-boot health check passed
Firmware marked valid. Rollback cancelled.
Published OTA status protonest/exampledevice/ota/status/update -> {"status":"completed","otaId":"357"}
```

The device name, IP address, timestamps, OTA ID, version, and firmware size will differ on your device.

If the broker later re-delivers a retained payload for a known OTA ID, the device ignores it silently.

## Troubleshooting

- If the sketch cannot connect, first confirm `x509_connect` works on the same board.
- If OTA download fails, check that `http-root-ca.pem` is present in LittleFS and matches `HTTPS_ROOT_CA_PATH`.
- If the OTA payload is ignored, verify the topic is `protonest/<device>/ota/pending`.
- If X.509 authentication fails, verify that the client certificate and key match the device.
- If the firmware does not fit or OTA behaves unexpectedly, check that `Arduino IDE > Tools > Partition Scheme > Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)` is selected.
- If rollback does not trigger, check both the partition scheme and whether the installed Arduino ESP32 core enables `CONFIG_APP_ROLLBACK_ENABLE`.

## What to try next

After X.509 OTA is working, this folder already gives you the complete Arduino IDE ESP32 foundation set.
