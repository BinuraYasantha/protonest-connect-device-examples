# `psk_connect`

`psk_connect` is the first Protonest Connect example you should try on Arduino IDE ESP32.

Use this sketch to prove four things before moving on to publish, subscribe, or OTA:

- your board can connect to Wi-Fi
- the device clock can sync for TLS certificate validation
- the Protonest broker CA file is readable from LittleFS
- MQTT username/password authentication works against `mqtt.protonest.co:8883`

## What this sketch does

This example:

- connects to Wi-Fi in station mode
- synchronizes time with NTP
- mounts LittleFS
- loads the Protonest broker CA certificate from LittleFS
- opens a TLS connection to `mqtt.protonest.co:8883`
- authenticates with MQTT username and password
- prints periodic status messages after it connects

Identity rule used by this example:

- the MQTT client ID and MQTT username both use the configured device name

## What success looks like

When this example is working, Serial Monitor should show:

- Wi-Fi connection progress
- successful clock synchronization
- MQTT connection to `mqtt.protonest.co:8883`
- repeating status lines with Wi-Fi state, MQTT state, RSSI, and device name

This example does not publish or subscribe to application topics yet. It is only a secure connection check.

## Files that matter

- [psk_connect.ino](../psk_connect.ino): Wi-Fi, TLS, MQTT connect, and status loop
- [Config.h](../Config.h): Wi-Fi settings, device name, MQTT password, broker host, and certificate path
- `data/`: LittleFS upload folder for `root-ca.crt`
- [docs/README.md](README.md): usage guide for this example

## Before you start

You need:

- Arduino IDE with the ESP32 core installed
- an ESP32 board selected in Arduino IDE
- the Arduino IDE 2 LittleFS uploader installed and working
- Wi-Fi credentials for the network your board should join
- Protonest PSK credentials for your device

If you need help installing the Arduino IDE 2 LittleFS uploader and using it, follow this guide:

- [Arduino IDE 2: Install ESP32 LittleFS Uploader](https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/)

## Protonest PSK credential files

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

Map those values into this sketch like this:

```text
DEVICE_NAME = example
MQTT_USERNAME = DEVICE_NAME
MQTT_CLIENT_ID = DEVICE_NAME
MQTT_PASSWORD = JvK82R17xQR!ZZx3K
```

`root-ca.crt` must be uploaded to LittleFS for this connect example.

`http-root-ca.pem` is included in the same ZIP for OTA examples. This connect example does not use it.

## What you must edit

Edit [Config.h](../Config.h) and replace the repo values with your own:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `DEVICE_NAME`
- `MQTT_PASSWORD`

Optional:

- `ROOT_CA_PATH` if you want to store the CA file under a different name in LittleFS

Important:

- do not commit real Wi-Fi credentials or MQTT passwords
- `DEVICE_NAME` should match the Protonest PSK username
- this sketch expects the MQTT username and client ID to be the same device name

## LittleFS file required

This example reads the broker CA certificate from LittleFS.

Copy this file into the local `data/` folder before uploading the filesystem:

- `root-ca.crt`

The default path expected by the sketch is:

```text
/root-ca.crt
```

If you use another filename, update `ROOT_CA_PATH` in [Config.h](../Config.h).

## Upload workflow

1. Open [psk_connect.ino](../psk_connect.ino) in Arduino IDE.
2. Edit [Config.h](../Config.h).
3. Copy `root-ca.crt` into the example `data/` folder.
4. Select the correct ESP32 board.
5. Select the correct serial port.
6. Close Serial Monitor.
7. Upload the `data/` folder to LittleFS.
8. Upload the sketch.
9. Open Serial Monitor at `115200`.

## Expected serial output

You should see output similar to this:

```text
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock...........
Clock synced: 1782905517
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice
MQTT connected
Status: wifi=connected mqtt=connected rssi=-66 device=exampledevice
Status: wifi=connected mqtt=connected rssi=-72 device=exampledevice
```

The IP address, timestamps, RSSI values, and device name will differ on your board.

## What this example verifies in code

Based on the implementation in [psk_connect.ino](../psk_connect.ino):

- TLS broker trust comes from `root-ca.crt` loaded from LittleFS
- time sync happens before the MQTT TLS connection attempt
- the sketch retries MQTT connection in the main loop
- status lines are printed every `STATUS_PRINT_INTERVAL_MS`

This example does not:

- subscribe to control topics
- publish telemetry
- handle OTA

## Troubleshooting

### Wi-Fi times out

Recheck `WIFI_SSID` and `WIFI_PASSWORD` in [Config.h](../Config.h), and make sure the board is in range of the access point.

### Clock sync fails

TLS setup depends on correct time. If NTP never succeeds, check:

- Wi-Fi connectivity
- whether the network allows access to `pool.ntp.org` or `time.nist.gov`

### LittleFS mount or CA load fails

Check:

- `root-ca.crt` was copied into `data/`
- the LittleFS upload completed before sketch upload
- `ROOT_CA_PATH` matches the uploaded filename

### MQTT authentication fails

Check:

- `DEVICE_NAME`
- `MQTT_PASSWORD`
- that `MQTT_USERNAME` and `MQTT_CLIENT_ID` still match `DEVICE_NAME`

### MQTT TLS connection fails

Check:

- `root-ca.crt` is the correct Protonest broker CA
- system time synchronized successfully before the MQTT connect attempt

### LittleFS upload fails

Close Serial Monitor and try the filesystem upload again.

## What to try next

After `psk_connect` works, continue with:

- `psk_publish` to send telemetry
- `psk_subscribe` to receive cloud-to-device messages
- `psk_ota` to test OTA delivery and rollback behavior
