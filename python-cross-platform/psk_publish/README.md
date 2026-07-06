# psk_publish

`psk_publish` sends sample telemetry to a Protonest Connect `stream` topic using PSK username/password authentication.

Use this after `psk_connect` is working.

## Files To Edit

Edit [`config.py`](config.py):

```python
DEVICE_NAME = "YOUR_DEVICE_NAME"
MQTT_PASSWORD = "YOUR_MQTT_PASSWORD"
```

From the downloaded PSK credentials:

- Use `PSA Username` as `DEVICE_NAME`.
- Use `PSA Password` as `MQTT_PASSWORD`.
- Copy `root-ca.crt` to `certs/root-ca.crt`.

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

## Install And Run

```powershell
python -m pip install -r requirements.txt
python main.py
```

## Expected Console Output

```text
Protonest Connect Python cross-platform PSK publish example
MQTT client_id == username == device name: exampledevice
Broker: mqtt.protonest.co:8883
Publish topic: protonest/exampledevice/stream/temperature
Root CA: ...\certs\root-ca.crt
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected: Success (0)
Published protonest/exampledevice/stream/temperature -> {"temperature_c": 24.5, "uptime_ms": 10000}
```

If this example publishes successfully, move to [`../psk_subscribe`](../psk_subscribe/).
