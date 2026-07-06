# x509_subscribe

`x509_subscribe` listens for Protonest Connect `state` commands using X.509 client certificate authentication.

When a command arrives on the configured state topic, the example republishes the same payload to `state/last-command` as an acknowledgement.

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
certs/device-cert.der
certs/device-key.der
```

Run `main.py` from Thonny.

## Expected Serial Output

```text
Protonest Connect Pico W MicroPython X.509 subscribe example
MQTT client_id == device name: exampledevice
No MQTT username/password is used for X.509 auth
Subscribe topic: protonest/exampledevice/state/motor
Ack topic: protonest/exampledevice/state/last-command
Connecting to Wi-Fi...
Wi-Fi connected: ('192.168.1.67', '255.255.255.0', '192.168.1.1', '192.168.1.1')
Syncing time...
Time synced: (2026, 6, 25, 11, 32, 52, 3, 176)
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected
Subscribed to: protonest/exampledevice/state/motor
Incoming protonest/exampledevice/state/motor -> {"status":true}
Ack protonest/exampledevice/state/last-command -> {"status":true}
```

If this example works, move to [`../x509_ota`](../x509_ota/).
