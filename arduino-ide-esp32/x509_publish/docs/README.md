# `x509_publish`

`x509_publish` is the Protonest telemetry example for ESP32 boards that authenticate with client certificates. Use it after `x509_connect` is already working and you want to verify the standard stream topic flow.

## What this example does

This sketch:

- connects to Protonest Connect with X.509 authentication
- publishes sample JSON telemetry to `protonest/<device>/stream/temperature`
- repeats the publish at a fixed interval

The payload is only test data generated inside the sketch. You can replace it later with real sensor values.

For this X.509 example, the identity rule is:

- X.509 auth does not use MQTT username/password
- the configured device name is used as the MQTT client ID

## Files in this folder

- [x509_publish.ino](../x509_publish.ino)
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
- stream name if you want a different telemetry channel

## Protonest X.509 credential files

When you download X.509 credentials from Protonest Connect, the ZIP file contains:

```text
example_x509.zip
├── root-ca.crt
├── <device>-cert.pem
├── <device>-key.pem
└── http-root-ca.pem
```

`http-root-ca.pem` is included in the same ZIP for OTA examples. This publish example does not use it.

This example already includes `root-ca.crt` at `data/root-ca.crt`. You do not need to copy it from the credential ZIP, but the same file is also visible inside the ZIP downloaded from the Protonest Connect console.

## LittleFS files required

This example reads all TLS assets from LittleFS.

Copy these device credential files into `data/` before uploading LittleFS:

- the client certificate file from the ZIP
- the client private key file from the ZIP

Then set these paths in [Config.h](../Config.h) using the actual filenames you copied into `data/`:

```cpp
constexpr char CLIENT_CERT_PATH[] = "/<device>-cert.pem";
constexpr char CLIENT_KEY_PATH[] = "/<device>-key.pem";
```

## Upload steps

1. Open [x509_publish.ino](../x509_publish.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Confirm `data/root-ca.crt` is present, then copy the client certificate and private key from the ZIP into `data/`.
4. Select the correct ESP32 board.
5. Select the correct serial port.
6. Close Serial Monitor.
7. Upload `data/` to LittleFS.
8. Upload the sketch.
9. Open Serial Monitor and watch the publish logs.

If you need help installing the Arduino IDE 2 LittleFS uploader and using it, follow this guide:

- [Arduino IDE 2: Install ESP32 LittleFS Uploader](https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/)

## Expected serial output

Serial Monitor should show output similar to this:

```text
Connecting to Wi-Fi....
Wi-Fi connected. IP: 192.168.1.26
Syncing clock.....
Clock synced: 1782904581
Connecting to MQTT broker mqtt.protonest.co:8883
MQTT connected
Publish protonest/exampledevice/stream/temperature -> {"temperature_c":24.5,"uptime_ms":10000} (ok)
Publish protonest/exampledevice/stream/temperature -> {"temperature_c":23.5,"uptime_ms":20000} (ok)
Publish protonest/exampledevice/stream/temperature -> {"temperature_c":24.5,"uptime_ms":30000} (ok)
```

The device name, IP address, timestamps, and telemetry values will differ on your device.

## Troubleshooting

- If the sketch cannot connect, first confirm `x509_connect` works on the same board.
- If MQTT connection fails, verify that the client certificate and key belong to the device name you configured.
- If TLS setup fails, confirm all required certificate files were uploaded to LittleFS.
- If nothing is published, recheck the configured stream name and monitor output.

## What to try next

After telemetry is working, continue with:

- `x509_subscribe` to test incoming state messages
- `x509_ota` to test OTA delivery
