# psk_subscribe

`psk_subscribe` listens for Protonest Connect `state` commands using PSK username/password authentication.

When a command arrives on the configured state topic, the example republishes the same payload to `state/last-command` as an acknowledgement.

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

## Topics

The example subscribes to:

```text
protonest/<device>/state/motor
```

The example publishes the acknowledgement to:

```text
protonest/<device>/state/last-command
```

Send a test command from MQTTX or the Protonest Connect console:

```json
{"status":true}
```

## Build And Flash

```powershell
idf.py set-target esp32c6
idf.py build flash monitor
```

## Expected Serial Output

```text
Protonest Connect ESP-IDF PSK subscribe example
MQTT client_id == username == device name: exampledevice
Subscribe topic: protonest/exampledevice/state/motor
Ack topic: protonest/exampledevice/state/last-command
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Connecting to MQTT broker mqtt.protonest.co:8883
MQTT connected
Subscribed to protonest/exampledevice/state/motor (ok, msg_id=1)
Incoming protonest/exampledevice/state/motor -> {"status":true}
Ack protonest/exampledevice/state/last-command -> {"status":true} (queued, msg_id=2)
```

If this example works, move to [`../psk_ota`](../psk_ota/).
