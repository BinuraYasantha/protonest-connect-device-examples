# `x509_subscribe`

`x509_subscribe` shows how an ESP32 device authenticated with client certificates can receive messages from Protonest Connect and publish an acknowledgement back. Use it after `x509_connect` is already working.

## What this example does

This sketch:

- connects to Protonest Connect with X.509 authentication
- subscribes to `protonest/<device>/state/motor`
- prints the incoming payload
- publishes the same payload to `protonest/<device>/state/last-command`

For this X.509 example, the identity rule is:

- X.509 auth does not use MQTT username/password
- the configured device name is used as the MQTT client ID

## Files in this folder

- [x509_subscribe.ino](../x509_subscribe.ino)
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
- topic suffixes if you want to test another state topic

## Protonest X.509 credential files

When you download X.509 credentials from Protonest Connect, the ZIP file contains:

```text
example_x509.zip
├── root-ca.crt
├── <device>-cert.pem
├── <device>-key.pem
└── http-root-ca.pem
```

`http-root-ca.pem` is included in the same ZIP for OTA examples. This subscribe example does not use it.

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

1. Open [x509_subscribe.ino](../x509_subscribe.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Confirm `data/root-ca.crt` is present, then copy the client certificate and private key from the ZIP into `data/`.
4. Select the correct ESP32 board.
5. Select the correct serial port.
6. Close Serial Monitor.
7. Upload `data/` to LittleFS.
8. Upload the sketch.
9. Publish a test message to `protonest/<device>/state/motor`.

If you need help installing the Arduino IDE 2 LittleFS uploader and using it, follow this guide:

- [Arduino IDE 2: Install ESP32 LittleFS Uploader](https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/)

## Expected serial output

Serial Monitor should show output similar to this:

```text
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock........
Clock synced: 1782964681
Connecting to MQTT broker mqtt.protonest.co:8883
Subscribed to protonest/exampledevice/state/motor (ok)
MQTT connected
Incoming protonest/exampledevice/state/motor -> {
    "status": true
}
Ack protonest/exampledevice/state/last-command (ok)
Incoming protonest/exampledevice/state/motor -> {
    "status": false
}
Ack protonest/exampledevice/state/last-command (ok)
```

The device name, IP address, timestamps, and message payloads will differ on your device.

## Troubleshooting

- If the sketch cannot connect, first confirm `x509_connect` works on the same board.
- If you do not receive messages, verify the published test topic matches the configured `state` topic.
- If acknowledgement is missing, check the incoming topic and the serial output.
- If X.509 authentication fails, verify that the client certificate and key match the device.

## What to try next

After subscribe is working, continue with:

- `x509_ota` to test OTA delivery
