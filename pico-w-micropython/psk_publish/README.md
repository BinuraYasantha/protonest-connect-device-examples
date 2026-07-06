# publish

`publish` sends sample telemetry to a Protonest Connect `stream` topic using PSK username/password authentication.

Use this after `connect` is working.

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

## Topic And Payload

The example publishes to:

```text
protonest/<device>/stream/temperature
```

Example payload:

```json
{"temperature_c":24.5,"uptime_ms":10000}
```

You can view published data in the Protonest Connect console:

```text
Projects > Devices > Manage Device
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
Protonest Connect Pico W MicroPython publish example
MQTT client_id == username == device name: exampledevice
Publish topic: protonest/exampledevice/stream/temperature
Connecting to Wi-Fi...
Wi-Fi connected: ('192.168.1.67', '255.255.255.0', '192.168.1.1', '192.168.1.1')
Syncing time...
Time synced: (2026, 6, 25, 3, 31, 51, 3, 176)
Connecting to MQTT broker mqtt.protonest.co:8883...
MQTT connected
Published protonest/exampledevice/stream/temperature -> {"temperature_c": 24.5, "uptime_ms": 10000}
```

If this example publishes successfully, move to [`../psk_subscribe`](../psk_subscribe/).
