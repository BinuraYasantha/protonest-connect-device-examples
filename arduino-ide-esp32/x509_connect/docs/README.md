# `x509_connect`

`x509_connect` is the smallest Protonest Connect example for ESP32 boards that authenticate with client certificates. Use it first when your device uses X.509 credentials and you want to validate the TLS and MQTT connection path before testing telemetry, commands, or OTA.

## What this example does

This sketch:

- connects to Wi-Fi
- synchronizes time for TLS certificate validation
- loads the broker CA certificate from LittleFS
- loads the client certificate and private key from LittleFS
- connects to `mqtt.protonest.co:8883`

For this X.509 example, the identity rule is:

- X.509 auth does not use MQTT username/password
- the configured device name is used as the MQTT client ID

## Files in this folder

- [x509_connect.ino](../x509_connect.ino)
- [Config.h](../Config.h)
- `data/` for LittleFS files
- [docs/README.md](README.md)

## What to edit

Edit [Config.h](../Config.h) and replace the placeholders with your own values:

- Wi-Fi SSID
- Wi-Fi password
- Protonest device name
- certificate paths if you use different filenames

## Protonest X.509 credential files

When you download X.509 credentials from Protonest Connect, the ZIP file contains:

```text
example_x509.zip
├── root-ca.crt
├── device-cert.pem
├── device-key.pem
└── http-root-ca.pem
```

`http-root-ca.pem` is included in the same ZIP for OTA examples. This connect example does not use it.

## LittleFS files required

This example reads all TLS assets from LittleFS.

Copy these files into the empty `data/` folder before uploading LittleFS:

- `root-ca.crt`
- `device-cert.pem`
- `device-key.pem`

If you want to use different filenames, update the paths in `Config.h`.

## Upload steps

1. Open [x509_connect.ino](../x509_connect.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Copy `root-ca.crt`, `device-cert.pem`, and `device-key.pem` into `data/`.
4. Select the correct ESP32 board.
5. Select the correct serial port.
6. Close Serial Monitor.
7. Upload `data/` to LittleFS.
8. Upload the sketch.
9. Open Serial Monitor.

If you need help installing the Arduino IDE 2 LittleFS uploader and using it, follow this guide:

- [Arduino IDE 2: Install ESP32 LittleFS Uploader](https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/)

## Expected serial output

Serial Monitor should show output similar to this:

```text
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock......
Clock synced: 1782972961
Connecting to MQTT broker mqtt.protonest.co:8883
MQTT connected
Status: wifi=connected mqtt=connected rssi=-66 device=exampledevice
Status: wifi=connected mqtt=connected rssi=-72 device=exampledevice
```

The device name, IP address, timestamps, and RSSI values will differ on your board.

## Troubleshooting

- If TLS setup fails, make sure all certificate files were uploaded to LittleFS.
- If X.509 authentication fails, verify that the client certificate and key match.
- If the broker rejects the connection, recheck the device name used as MQTT client ID.
- If LittleFS upload fails, close Serial Monitor and try again.

## What to try next

If this example works, continue with:

- `x509_publish` to test telemetry
- `x509_subscribe` to test cloud-to-device commands
- `x509_ota` to test firmware updates
