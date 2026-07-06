# Protonest Connect ESP-IDF Examples

This folder contains standalone ESP-IDF examples for ESP32 boards with Wi-Fi. Open an example folder, edit `main/Config.h`, replace the certificate files when needed, then build and flash with ESP-IDF.

## Start Here

Use the PSK examples when your Protonest Connect device uses username/password credentials.

| Step | Example | Purpose |
| --- | --- | --- |
| 1 | [`psk_connect`](psk_connect/) | Verify Wi-Fi, TLS, and MQTT login |
| 2 | [`psk_publish`](psk_publish/) | Publish telemetry to a `stream` topic |
| 3 | [`psk_subscribe`](psk_subscribe/) | Receive `state` commands and publish an ack |
| 4 | [`psk_ota`](psk_ota/) | Receive OTA requests, download firmware over HTTPS, and validate rollback |

Use the X.509 examples when your Protonest Connect device uses a client certificate and private key.

| Step | Example | Purpose |
| --- | --- | --- |
| 1 | [`x509_connect`](x509_connect/) | Verify Wi-Fi, TLS, client certificate auth, and MQTT connect |
| 2 | [`x509_publish`](x509_publish/) | Publish telemetry to a `stream` topic |
| 3 | [`x509_subscribe`](x509_subscribe/) | Receive `state` commands and publish an ack |
| 4 | [`x509_ota`](x509_ota/) | Receive OTA requests, download firmware over HTTPS, and validate rollback |

## Common Workflow

1. Open one example folder, such as `psk_connect`.
2. Edit `main/Config.h`.
3. Replace the files in `main/certs/` with your downloaded Protonest Connect credentials.
4. Set the ESP-IDF target for your board, for example `idf.py set-target esp32c6`.
5. Run `idf.py build flash monitor`.

The examples embed certificate files at build time using `target_add_binary_data()` in each `CMakeLists.txt`. There is no LittleFS/SPIFFS upload step for ESP-IDF examples.

## Authentication Model

For PSK examples, copy these values from the downloaded PSK credentials:

- Wi-Fi SSID and password
- `DEVICE_NAME` from `PSA Username`
- `MQTT_PASSWORD` from `PSA Password`

For X.509 examples, copy these values from the downloaded X.509 credentials:

- Wi-Fi SSID and password
- `DEVICE_NAME`, which must match the device certificate common name
- `root-ca.crt`
- `device-cert.pem`
- `device-key.pem`

X.509 examples do not use MQTT username or password. The MQTT client ID is the device name, and the client certificate is used for authentication.

Keep `DEVICE_NAME`, MQTT client ID, the registered device identity, and the client certificate identity aligned. The examples validate this where possible, and the broker will reject mismatched identities.

## Credential ZIP Files

A PSK credential ZIP contains:

```text
example_psk.zip
|-- root-ca.crt
|-- example-creds.txt
`-- http-root-ca.pem
```

The `example-creds.txt` file contains values like:

```text
PSA Username: example
PSA Password: JvK82R17xQR!ZZx3K
```

For PSK examples, use `PSA Username` as `DEVICE_NAME` and `PSA Password` as `MQTT_PASSWORD`.

An X.509 credential ZIP contains the MQTT broker CA, device certificate, private key, and `http-root-ca.pem` for OTA downloads. The connect, publish, and subscribe examples only need the MQTT/TLS files. The OTA examples need both the MQTT broker CA and the HTTPS OTA download CA.

## MQTT Topic Rules

All examples follow the Protonest Connect MQTT topic rules:

- Level 1 is always `protonest`.
- Level 2 is always the device name.
- Level 3 must be `stream`, `state`, or `ota`.

The third level defines the feature area:

- `stream` is used for sensor data and telemetry-like messages.
- `state` is used for actuator state, device state, and command/operation flows.
- `ota` is used for OTA update request and status flows.

Example topics:

```text
protonest/<device>/stream/temperature
protonest/<device>/state/motor
protonest/<device>/state/last-command
protonest/<device>/ota/pending
protonest/<device>/ota/status/update
```

`protonest/<device>/state/last-command` is an example acknowledgement topic used by the subscription examples to confirm that an incoming command was received.

## OTA Notes

The OTA examples commit the required partition and rollback settings in each example's `sdkconfig`. If you change them, keep these options enabled:

```text
CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

If you previously flashed a non-OTA example to the same board, run a full erase once before flashing an OTA example:

```powershell
idf.py erase-flash
```

Each OTA example README explains the OTA payload, test flow, rollback validation, and expected serial output.
