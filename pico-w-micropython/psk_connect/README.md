# connect

`connect` is the first Pico W MicroPython example to run when your Protonest Connect device uses PSK username/password authentication.

It verifies Wi-Fi, TLS certificate validation, and MQTT login to `mqtt.protonest.co:8883`.

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
Protonest Connect Pico W MicroPython connect example
MQTT client_id == username == device name: exampledevice
Connecting to Wi-Fi...
Wi-Fi connected: ('192.168.1.67', '255.255.255.0', '192.168.1.1', '192.168.1.1')
Syncing time...
Time synced: (2026, 6, 25, 3, 29, 43, 3, 176)
Connecting to MQTT broker mqtt.protonest.co:8883...
MQTT connected
Status: wifi=connected mqtt=connected ip=192.168.1.67 device=exampledevice
```

If this example connects successfully, move to [`../psk_publish`](../psk_publish/).
