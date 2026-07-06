# subscribe

`subscribe` listens for Protonest Connect `state` commands using PSK username/password authentication.

When a command arrives on the configured state topic, the example republishes the same payload to `state/last-command` as an acknowledgement.

## Files To Edit

Edit [`config.py`](config.py):

```python
WIFI_SSID = "YOUR_WIFI_SSID"
WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"
DEVICE_NAME = "YOUR_DEVICE_NAME"
MQTT_PASSWORD = "YOUR_MQTT_PASSWORD"
```

From the downloaded PSK credentials:

- Use `PSA Username` as `DEVICE_NAME`.
- Use `PSA Password` as `MQTT_PASSWORD`.
- Copy the MQTT broker root CA to `/certs/root-ca.der` on the Pico.

Convert the downloaded PEM file to DER first. See [Convert PEM Files To DER](../README.md#convert-pem-files-to-der).

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

## Upload To Pico

Copy these files and folders to the Pico filesystem:

```text
main.py
config.py
umqtt/
certs/root-ca.der
```

Run `main.py` from Thonny.

## Expected Serial Output

```text
Protonest Connect Pico W MicroPython subscribe example
MQTT client_id == username == device name: exampledevice
Subscribe topic: protonest/exampledevice/state/motor
Ack topic: protonest/exampledevice/state/last-command
Connecting to Wi-Fi...
Wi-Fi connected: ('192.168.1.67', '255.255.255.0', '192.168.1.1', '192.168.1.1')
Syncing time...
Time synced: (2026, 6, 25, 3, 41, 41, 3, 176)
Connecting to MQTT broker mqtt.protonest.co:8883...
MQTT connected
Subscribed to: protonest/exampledevice/state/motor
Incoming protonest/exampledevice/state/motor -> {"status":true}
Ack protonest/exampledevice/state/last-command -> {"status":true}
```

If this example works, move to [`../psk_ota`](../psk_ota/).
