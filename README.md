# Protonest Connect Device Examples

This repository contains code samples for connecting devices and applications to Protonest Connect.

The goal is simple:

- show how to connect a device to `mqtt.protonest.co:8883`
- keep all credentials and certificate material user-supplied
- provide examples that are easy to open, configure, build, upload, and test
- support multiple hardware and software platforms in one place

## Purpose

Use these examples as practical starting projects for Protonest Connect device integration. Each folder is organized so developers can open one example, add their own device credentials, build it, and test the matching Protonest Connect feature.

## Example Folders

| Folder | Platform | Start here |
| --- | --- | --- |
| [`arduino-ide-esp32`](arduino-ide-esp32/) | ESP32 using Arduino IDE | [`arduino-ide-esp32/README.md`](arduino-ide-esp32/README.md) |
| [`esp-idf`](esp-idf/) | ESP32 using ESP-IDF | [`esp-idf/README.md`](esp-idf/README.md) |
| [`pico-w-micropython`](pico-w-micropython/) | Raspberry Pi Pico W / Pico 2 W using MicroPython | [`pico-w-micropython/README.md`](pico-w-micropython/README.md) |
| [`python-cross-platform`](python-cross-platform/) | Windows, Linux, macOS, and Raspberry Pi SBCs using Python | [`python-cross-platform/README.md`](python-cross-platform/README.md) |

Each platform folder should be usable as a foundation project: open the folder, follow its README, replace placeholders with your own Protonest Connect credentials, and run the example.

## Authentication modes

Each supported platform is expected to cover both of these authentication methods:

- `PSK` using MQTT username and password
- `X.509` using client certificates

## MQTT Topic Rules

Protonest Connect MQTT topics follow these platform rules:

- topic level 1 must be `protonest`
- topic level 2 must be the device name
- topic level 3 must be `stream`, `state`, or `ota`

The third level defines the feature area:

- `stream` is used for sensor data and telemetry-like messages
- `state` is used for actuator state, device state, and command/operation flows
- `ota` is used for OTA update request and status flows

Example topics:

- `protonest/<device>/stream/<name>`
- `protonest/<device>/state/<name>`
- `protonest/<device>/state/motor`
- `protonest/<device>/state/last-command`
- `protonest/<device>/ota/pending`
- `protonest/<device>/ota/status/update`

`protonest/<device>/state/last-command` is an example acknowledgement topic used by the subscription examples to confirm that an incoming command was received.

The device name is user-configured. For PSK examples, MQTT `client_id == username == device name`. For X.509 examples, MQTT `client_id == device name` and username/password are not used.

For X.509 examples, keep `DEVICE_NAME`, MQTT client ID, the registered device identity, and the client certificate identity aligned. Some examples validate this locally; others fail at broker authentication if they do not match.

## Example Types

Each platform folder includes the same main example types where supported:

- `connect` examples verify Wi-Fi or network access, TLS, authentication, and MQTT connection.
- `publish` examples send sample sensor data to a `stream` topic.
- `subscribe` examples receive messages from a `state` topic and publish an acknowledgement.
- `ota` examples receive OTA requests, download the update file, apply the update, and publish OTA status.

Platform-specific setup, payload details, rollback behavior, and expected output are documented inside each platform folder and example README.

## Running The Examples

Before running an example, create or select a device in the Protonest Connect web console and download the matching credentials for that device.

Use the downloaded credential files with the example you want to run:

- For PSK examples, copy the device name from the credentials text file into the example configuration and copy the MQTT password into `MQTT_PASSWORD`.
- For X.509 examples, copy the device certificate, private key, and root CA files into the certificate folder documented by that example.
- For OTA examples, also copy the HTTPS root CA file from the downloaded credential ZIP when the example requires it.
- Keep Wi-Fi credentials, device credentials, certificates, and private keys local to your working copy.

Each example README lists the exact files to copy, the configuration values to edit, and the expected serial or console output.
