# x509_publish

`x509_publish` sends sample telemetry to a Protonest Connect `stream` topic using X.509 client certificate authentication.

Use this after `x509_connect` is working.

## Files To Edit

Edit [`config.py`](config.py):

```python
WIFI_SSID = "YOUR_WIFI_SSID"
WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"
DEVICE_NAME = "YOUR_DEVICE_NAME"
```

From the downloaded X.509 credentials:

- Copy the MQTT broker root CA to `/certs/root-ca.der` on the Pico.
- Copy the device certificate to `/certs/device-cert.der` on the Pico.
- Copy the device private key to `/certs/device-key.der` on the Pico.

Convert the downloaded PEM files to DER first. See [Convert PEM Files To DER](../README.md#convert-pem-files-to-der).

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

## Upload To Pico

Copy these files and folders to the Pico filesystem:

```text
main.py
config.py
umqtt/
certs/root-ca.der
certs/device-cert.der
certs/device-key.der
```

Run `main.py` from Thonny.

## Expected Serial Output

```text
Protonest Connect Pico W MicroPython X.509 publish example
MQTT client_id == device name: exampledevice
No MQTT username/password is used for X.509 auth
Publish topic: protonest/exampledevice/stream/temperature
Connecting to Wi-Fi...
Wi-Fi connected: ('192.168.1.67', '255.255.255.0', '192.168.1.1', '192.168.1.1')
Syncing time...
Time synced: (2026, 6, 25, 11, 28, 11, 3, 176)
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected
Published protonest/exampledevice/stream/temperature -> {"temperature_c": 24.5, "uptime_ms": 10000}
```

If this example publishes successfully, move to [`../x509_subscribe`](../x509_subscribe/).
