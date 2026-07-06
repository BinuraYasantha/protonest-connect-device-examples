# psk_publish

`psk_publish` publishes sample telemetry to a Protonest Connect `stream` topic using PSK username/password authentication.

Use this after `psk_connect` is working.

## Files To Edit

Edit [`main/Config.h`](main/Config.h):

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define DEVICE_NAME "YOUR_DEVICE_NAME"
#define MQTT_PASSWORD "YOUR_MQTT_PASSWORD"
```

From the downloaded PSK ZIP:

- Copy `root-ca.crt` into `main/certs/root-ca.crt`.
- Use `PSA Username` as `DEVICE_NAME`.
- Use `PSA Password` as `MQTT_PASSWORD`.

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
Protonest Connect ESP-IDF PSK publish example
MQTT client_id == username == device name: exampledevice
Stream topic: protonest/exampledevice/stream/temperature
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Connecting to MQTT broker mqtt.protonest.co:8883
MQTT connected
Publish protonest/exampledevice/stream/temperature -> {"temperature_c":24.5,"uptime_ms":10000} (queued, msg_id=1)
Publish protonest/exampledevice/stream/temperature -> {"temperature_c":23.5,"uptime_ms":15000} (queued, msg_id=2)
```

If this example publishes successfully, move to [`../psk_subscribe`](../psk_subscribe/).
