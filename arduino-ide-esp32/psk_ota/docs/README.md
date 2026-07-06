# `psk_ota`

`psk_ota` is the Protonest Connect Arduino IDE ESP32 OTA example for PSK authentication.

This example keeps the OTA flow intentionally small:

- MQTT trigger over `mqtt.protonest.co:8883`
- HTTPS firmware download
- OTA image write with `Update`
- reboot into the new image
- post-boot confirmation after Wi-Fi and MQTT recover
- bootloader rollback when the installed Arduino ESP32 core supports it

## Why this example is structured this way

This example shows the OTA and rollback pattern with a small amount of state, so the flow is easier to understand and debug.

The main idea is:

1. Accept only newer OTA IDs.
2. Store one pending OTA record before reboot.
3. Reboot into the new image.
4. Confirm the image only after the new firmware reconnects cleanly.
5. Let the bootloader roll back if that health check never succeeds.

## Best beginner approach for OTA and rollback

For Arduino IDE ESP32 projects, the safest beginner-friendly pattern is:

1. Use an OTA partition scheme.
2. Download firmware over HTTPS, not plain HTTP.
3. Stage a pending OTA ID in `Preferences` before reboot.
4. Do not publish `completed` immediately after writing the image.
5. Publish `completed` only after the new firmware boots and reconnects.
6. Use bootloader rollback support when the Arduino ESP32 core exposes `CONFIG_APP_ROLLBACK_ENABLE`.

That gives you a simple mental model:

- `pending` means "new image written, not trusted yet"
- `completed` means "new image booted and proved it can reconnect"
- rollback happens if the image never becomes healthy after boot

## What this sketch stores

This example stores only three pieces of OTA metadata in `Preferences`:

- confirmed OTA ID
- pending OTA ID
- pending OTA version

It does not keep a full history. That is deliberate. The goal is to keep the first OTA example easy to explain and debug.

The stored pending version is also used to tell the difference between:

- a real boot into the new firmware
- a rollback back into the previous firmware

## What you must edit

Edit [Config.h](../Config.h) and replace the placeholders:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `DEVICE_NAME`
- `MQTT_PASSWORD`
- `APP_VERSION`

Identity rule used by this sketch:

- the MQTT client ID and MQTT username both use the configured device name

Before building a new OTA release, bump `APP_VERSION` so the new firmware can recognize itself after reboot and confirm the pending OTA safely.

## Files required in `data/`

When you download PSK credentials from Protonest Connect, the ZIP file contains:

```text
example_psk.zip
├── root-ca.crt
├── example-creds.txt
└── http-root-ca.pem
```

Create a `data/` folder inside `psk_ota` before uploading LittleFS, then copy in:

- `root-ca.crt`
- `http-root-ca.pem`

How they are used:

- `root-ca.crt` secures MQTT to `mqtt.protonest.co`
- `http-root-ca.pem` secures the HTTPS firmware download

The file paths expected by the sketch are:

- `/root-ca.crt`
- `/http-root-ca.pem`

## Recommended partition scheme

Use an Arduino IDE partition scheme that supports OTA, for example:

- `Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)`

That gives you:

- two OTA application slots
- enough filesystem space for the CA files

If your compiled firmware does not fit inside one OTA app slot, OTA will fail.

## Upload workflow

1. Open [psk_ota.ino](../psk_ota.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Create a local `data/` folder in the example directory.
4. Copy `root-ca.crt` and `http-root-ca.pem` into `data/`.
5. Select the correct ESP32 board.
6. Select an OTA-capable partition scheme.
7. Close Serial Monitor.
8. Upload `data/` to LittleFS.
9. Upload the sketch.

## OTA topic and payload

This sketch listens on:

- `protonest/<device>/ota/pending`

and publishes status to:

- `protonest/<device>/ota/status/update`

Expected payload:

```json
{
  "otaId": 351,
  "version": "1.1.0",
  "file_size": 1189648,
  "url": "https://api.protonestconnect.co/api/v1/ota/download/351/example-device?token=example-token"
}
```

Required fields:

- `otaId`
- `version`
- `url`

Optional but recommended:

- `file_size`

If `file_size` is present and the server returns a different size, the update is rejected.

## OTA status behavior

This example publishes:

- `failed` for payload, download, or flashing problems
- `rejected` for blocked OTA IDs or new updates received while firmware confirmation is still pending
- `completed` only after the new firmware boots and reconnects
- known retained OTA IDs are ignored silently

Example success payload:

```json
{
  "status": "completed",
  "otaId": "351"
}
```

## Post-boot confirmation flow

After the new image boots, this sketch waits until:

- Wi-Fi is connected
- MQTT is connected
- `POST_BOOT_CONFIRM_DELAY_MS` has elapsed

Only then does it:

1. cancel rollback if bootloader rollback is active
2. save the confirmed OTA ID
3. publish `completed`
4. clear the pending OTA record

If rollback support is active and the device never becomes healthy before `POST_BOOT_VERIFY_TIMEOUT_MS`, the sketch requests rollback.

## Important limitation

Automatic rollback depends on the installed Arduino ESP32 core exposing `CONFIG_APP_ROLLBACK_ENABLE`.

If rollback support is not enabled in the core:

- this sketch still stages a pending OTA record
- this sketch still confirms only after reconnecting
- but the bootloader cannot automatically restore the previous image

## Expected serial output

Serial Monitor should show output similar to this:

```text
==================================================
Protonest Connect Arduino OTA Example
Transport: MQTT + HTTPS
Mode: PSK
Rollback support: enabled
MQTT client_id == username == device name: exampledevice
App version: 0.0.1
==================================================
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock......
Clock synced: 1782972961
Mounting LittleFS and loading TLS assets
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice
Subscribed to protonest/exampledevice/ota/pending (ok)
MQTT connected
Received OTA request payload
Topic: protonest/exampledevice/ota/pending
Starting OTA update id=357 version=1.1.0
OTA URL: https://api.protonestconnect.co/api/v1/ota/download/357/exampledevice?token=example-token
OTA HTTP status: 200
OTA content-length: 1189648
Downloading firmware [................................]   0% (4096/1189648 bytes)
Downloading firmware [########........................]  25% (297412/1189648 bytes)
Downloading firmware [################................]  50% (594824/1189648 bytes)
Downloading firmware [########################........]  75% (892236/1189648 bytes)
...
Downloading firmware [################################] 100% (1189648/1189648 bytes)
OTA bytes written: 1189648
OTA image written successfully
Rebooting into the new firmware
New OTA image is pending bootloader verification
Pending OTA metadata: otaId=357 version=1.1.0
Waiting for post-boot health check before confirming firmware
Connecting to Wi-Fi.....
Wi-Fi connected. IP: 192.168.1.26
Syncing clock
Clock synced: 1782973032
Mounting LittleFS and loading TLS assets
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice
Subscribed to protonest/exampledevice/ota/pending (ok)
MQTT connected
Post-boot health check passed
Firmware marked valid. Rollback cancelled.
Published OTA status protonest/exampledevice/ota/status/update -> {"status":"completed","otaId":"357"}
```

The device name, IP address, timestamps, OTA ID, version, token, and firmware size will differ on your device.

If the broker later re-delivers a retained payload for a known OTA ID, the device ignores it silently.

## Troubleshooting

- If MQTT cannot connect, first make sure a PSK connect example already works on the same board.
- If HTTPS OTA fails before download starts, check `http-root-ca.pem` and the OTA URL certificate chain.
- If the firmware is written but never reaches `completed`, watch for Wi-Fi or MQTT reconnect failures after reboot.
- If an OTA is ignored, check whether the same `otaId` was already confirmed or is still pending.
- If the image does not fit, switch to a partition scheme with enough OTA slot space.
