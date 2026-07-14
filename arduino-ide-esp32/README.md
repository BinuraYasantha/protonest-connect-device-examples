# Arduino IDE ESP32 Examples

This folder contains Protonest Connect examples for ESP32 boards using the Arduino core with the Arduino IDE workflow.

Use this README as a starting point only. Each example has its own detailed guide in `docs/README.md` with the exact `Config.h` values to edit, LittleFS files to upload, expected serial output, and troubleshooting notes.

## Start Here

Choose one authentication path and begin with the matching `connect` example.

- `PSK`: `psk_connect` -> `psk_publish` -> `psk_subscribe` -> `psk_ota`
- `X.509`: `x509_connect` -> `x509_publish` -> `x509_subscribe` -> `x509_ota`

After the `connect` example works, you can test publish and subscribe in either order. Try OTA after the connection path is stable.

## Before You Open An Example

- Install the Arduino ESP32 core before using these examples.
- Install the Arduino IDE 2 LittleFS uploader.
- Install `PubSubClient` for MQTT examples.
- Install `ArduinoJson` for OTA examples.
- Select the correct ESP32 board and serial port before upload.
- For OTA sketches, select an OTA-capable partition scheme such as `Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)`.

## Example Map

| Example | Auth | Purpose | Detailed guide |
| --- | --- | --- | --- |
| `psk_connect` | PSK | First secure broker connection check | [psk_connect/docs/README.md](./psk_connect/docs/README.md) |
| `psk_publish` | PSK | Publish sample telemetry to a `stream` topic | [psk_publish/docs/README.md](./psk_publish/docs/README.md) |
| `psk_subscribe` | PSK | Receive a `state` message and publish an acknowledgement | [psk_subscribe/docs/README.md](./psk_subscribe/docs/README.md) |
| `psk_ota` | PSK | Receive OTA requests and report OTA status | [psk_ota/docs/README.md](./psk_ota/docs/README.md) |
| `x509_connect` | X.509 | First secure broker connection check with client certificates | [x509_connect/docs/README.md](./x509_connect/docs/README.md) |
| `x509_publish` | X.509 | Publish sample telemetry to a `stream` topic | [x509_publish/docs/README.md](./x509_publish/docs/README.md) |
| `x509_subscribe` | X.509 | Receive a `state` message and publish an acknowledgement | [x509_subscribe/docs/README.md](./x509_subscribe/docs/README.md) |
| `x509_ota` | X.509 | Receive OTA requests and report OTA status | [x509_ota/docs/README.md](./x509_ota/docs/README.md) |

## Shared Notes

- All examples use LittleFS for certificate and TLS-related files.
- Each example includes `data/root-ca.crt` for MQTT broker TLS verification. The same file is also visible in the credential ZIP downloaded from the Protonest Connect console.
- The per-example guide tells you which additional files belong in that example's local `data/` folder before the LittleFS upload.
- Downloaded PSK and X.509 ZIP files include `http-root-ca.pem`. The OTA examples use it for HTTPS firmware downloads.
- PSK examples use the device name as both the MQTT `client_id` and MQTT username.
- X.509 examples use the device name as the MQTT `client_id` and do not use MQTT username or password.
- For X.509, keep `DEVICE_NAME`, MQTT client ID, the registered device identity, and the client certificate identity aligned.
- Do not commit real Wi-Fi credentials, MQTT passwords, device certificates, or private keys.

## MQTT Topic Rules

Protonest Connect MQTT topics follow these platform rules:

- Level 1 must be `protonest`.
- Level 2 must be the device name.
- Level 3 must be `stream`, `state`, or `ota`.

The third level defines the feature area:

- `stream` is used for sensor data and telemetry-like messages.
- `state` is used for actuator state, device state, and command/operation flows.
- `ota` is used for OTA update request and status flows.

`protonest/<device>/state/last-command` is an example acknowledgement topic used by the subscription examples to confirm that an incoming command was received.

The exact topics and payloads differ by example. Check the matching `docs/README.md` before testing publish, subscribe, or OTA behavior.

## Folder Layout

Each example folder includes:

- the sketch file, such as `psk_connect.ino`
- `Config.h`
- `data/`
- `docs/README.md`

Use the example-level guide for the full setup and test procedure.
