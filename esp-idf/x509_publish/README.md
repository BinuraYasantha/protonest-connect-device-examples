# x509_publish

`x509_publish` publishes sample telemetry to a Protonest Connect `stream` topic using X.509 client certificate authentication.

Use this after `x509_connect` is working.

## Files To Edit

Edit [`main/Config.h`](main/Config.h):

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define DEVICE_NAME "YOUR_DEVICE_NAME"
```

From the downloaded X.509 ZIP:

- Copy `root-ca.crt` into `main/certs/root-ca.crt`.
- Copy the device certificate into `main/certs/device-cert.pem`.
- Copy the device private key into `main/certs/device-key.pem`.

No MQTT username or MQTT password is needed. The MQTT client ID is the device name.

## Topic And Payload

The example publishes to:

```text
protonest/<device>/stream/temperature
```

Example payload:

```json
{"temperature_c":24.5,"uptime_ms":10000}
```

You can view published stream data in the Protonest Connect console:

```text
Projects > Devices > Manage Device
```

## Build And Flash

```powershell
idf.py set-target esp32c6
idf.py build flash monitor
```

## Expected Serial Output

```text
Protonest Connect ESP-IDF X.509 publish example
MQTT client_id == device name: exampledevice
Stream topic: protonest/exampledevice/stream/temperature
Device certificate CN: exampledevice
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock...
Clock synced: 1782905517
Connecting to MQTT broker mqtt.protonest.co:8883
MQTT connected
Publish protonest/exampledevice/stream/temperature -> {"temperature_c":24.5,"uptime_ms":10000} (queued, msg_id=1)
```

If this example publishes successfully, move to [`../x509_subscribe`](../x509_subscribe/).
