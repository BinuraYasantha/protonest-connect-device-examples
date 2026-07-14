# `psk_subscribe`

`psk_subscribe` demonstrates the receive side of a Protonest device connection. Use it after `psk_connect` is working when you want to test incoming state updates or simple cloud-to-device commands.

## What this example does

This sketch:

- connects to Protonest Connect with MQTT username/password authentication
- subscribes to `protonest/<device>/state/motor`
- prints the incoming payload
- publishes the same payload to `protonest/<device>/state/last-command`

For this PSK example, the identity rule is:

- the MQTT client ID and MQTT username both use the configured device name

## Protonest credential files

When you download PSK credentials from Protonest Connect, the ZIP file contains:

- `root-ca.crt`
- `<device>-creds.txt`
- `http-root-ca.pem`

Example:

```text
example_psk.zip
├── root-ca.crt
├── example-creds.txt
└── http-root-ca.pem
```

Example `example-creds.txt` content:

```text
PSA Username: example
PSA Password: JvK82R17xQR!ZZx3K
```

Replace only these values in [Config.h](../Config.h):

```text
DEVICE_NAME = example
MQTT_PASSWORD = JvK82R17xQR!ZZx3K
```

This example already includes `root-ca.crt` at `data/root-ca.crt`. You do not need to copy it from the credential ZIP, but the same file is also visible inside the ZIP downloaded from the Protonest Connect console.

`http-root-ca.pem` is included in the same ZIP for OTA examples. This subscribe example does not use it.

For this example, copy:

- `PSA Username` into `DEVICE_NAME`
- `PSA Password` into `MQTT_PASSWORD`

## Files in this folder

- [psk_subscribe.ino](../psk_subscribe.ino)
- [Config.h](../Config.h)
- `data/` for LittleFS files
- [docs/README.md](README.md)

## What to edit

Edit [Config.h](../Config.h) and replace the placeholders with your own values:

- Wi-Fi SSID
- Wi-Fi password
- Protonest device name
- Protonest MQTT password
- topic suffixes if you want to test another state topic

Set `DEVICE_NAME` from `PSA Username` and `MQTT_PASSWORD` from `PSA Password`.

## LittleFS files required

This example reads the broker CA certificate from LittleFS.

This CA file is already provided in the local `data/` folder and is uploaded with LittleFS:

- `root-ca.crt`

## Upload steps

1. Open [psk_subscribe.ino](../psk_subscribe.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Confirm `data/root-ca.crt` is present.
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

- If the sketch cannot connect, first confirm `psk_connect` works on the same board.
- If you do not receive messages, verify the published test topic matches the configured `state` topic.
- If acknowledgement is missing, check the incoming topic and the serial output.
- If TLS setup fails, confirm `root-ca.crt` was uploaded to LittleFS.

## What to try next

After subscribe is working, continue with:

- `psk_ota` to test OTA delivery
