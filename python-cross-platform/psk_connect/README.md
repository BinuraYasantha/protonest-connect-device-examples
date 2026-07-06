# psk_connect

`psk_connect` is the first cross-platform Python example to run when your Protonest Connect device uses PSK username/password authentication.

It verifies TLS certificate validation and MQTT login to `mqtt.protonest.co:8883`.

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

The example sets `MQTT_USERNAME = DEVICE_NAME` and `MQTT_CLIENT_ID = DEVICE_NAME`.

## Install And Run

```powershell
python -m pip install -r requirements.txt
python main.py
```

## Expected Console Output

```text
Protonest Connect Python cross-platform PSK connect example
MQTT client_id == username == device name: exampledevice
Broker: mqtt.protonest.co:8883
Root CA: ...\certs\root-ca.crt
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected: Success (0)
Status: mqtt=connected broker=mqtt.protonest.co:8883 device=exampledevice
```

If this example connects successfully, move to [`../psk_publish`](../psk_publish/).
