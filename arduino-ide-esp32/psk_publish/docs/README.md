# `psk_publish`

`psk_publish` is the first telemetry example in the Protonest Arduino ESP32 suite. Use it after `psk_connect` is already working and you want to verify the standard Protonest stream topic flow.

## What this example does

This sketch:

- connects to Protonest Connect with MQTT username/password authentication
- publishes sample JSON telemetry to `protonest/<device>/stream/temperature`
- repeats the publish at a fixed interval

The payload is only test data generated inside the sketch. You can replace it later with real sensor values.

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

`http-root-ca.pem` is included in the same ZIP for OTA examples. This publish example does not use it.

For this example, copy:

- `PSA Username` into `DEVICE_NAME`
- `PSA Password` into `MQTT_PASSWORD`

## Files in this folder

- [psk_publish.ino](../psk_publish.ino)
- [Config.h](../Config.h)
- `data/` for LittleFS files
- [docs/README.md](README.md)

## What to edit

Edit [Config.h](../Config.h) and replace the placeholders with your own values:

- Wi-Fi SSID
- Wi-Fi password
- Protonest device name
- Protonest MQTT password
- stream name if you want a different telemetry channel

Set `DEVICE_NAME` from `PSA Username` and `MQTT_PASSWORD` from `PSA Password`.

## LittleFS files required

This example reads the broker CA certificate from LittleFS.

This CA file is already provided in the local `data/` folder and is uploaded with LittleFS:

- `root-ca.crt`

## Upload steps

1. Open [psk_publish.ino](../psk_publish.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Confirm `data/root-ca.crt` is present.
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

You can also verify the published data in the Protonest Console:

1. Open `https://console.protonestconnect.co/`
2. Go to `Projects`
3. Click `Devices`
4. Find your device
5. Click `Manage Device`

## Troubleshooting

- If the sketch cannot connect, first confirm `psk_connect` works on the same board.
- If MQTT authentication fails, verify the device name and MQTT password.
- If TLS setup fails, confirm `root-ca.crt` was uploaded to LittleFS.
- If nothing is published, recheck the configured stream name and monitor output.

## What to try next

After telemetry is working, continue with:

- `psk_subscribe` to test incoming state messages
- `psk_ota` to test OTA delivery
