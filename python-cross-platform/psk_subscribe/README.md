# psk_subscribe

`psk_subscribe` listens for Protonest Connect `state` commands using PSK username/password authentication.

When a command arrives on the configured state topic, the example republishes the same payload to `state/last-command` as an acknowledgement.

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

## Install And Run

```powershell
python -m pip install -r requirements.txt
python main.py
```

## Expected Console Output

```text
Protonest Connect Python cross-platform PSK subscribe example
MQTT client_id == username == device name: exampledevice
Broker: mqtt.protonest.co:8883
Subscribe topic: protonest/exampledevice/state/motor
Ack topic: protonest/exampledevice/state/last-command
Root CA: ...\certs\root-ca.crt
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected: Success (0)
Subscribed to: protonest/exampledevice/state/motor
Incoming protonest/exampledevice/state/motor -> {"status":true}
Ack queued protonest/exampledevice/state/last-command -> {"status":true}
Ack published protonest/exampledevice/state/last-command -> {"status":true}
```

If this example works, move to [`../psk_ota`](../psk_ota/).
