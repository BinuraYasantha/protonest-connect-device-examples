# x509_subscribe

`x509_subscribe` listens for Protonest Connect `state` commands using X.509 client certificate authentication.

When a command arrives on the configured state topic, the example republishes the same payload to `state/last-command` as an acknowledgement.

## Files To Edit

Edit [`config.py`](config.py):

```python
DEVICE_NAME = "YOUR_DEVICE_NAME"
```

From the downloaded X.509 credentials:

- Copy `root-ca.crt` to `certs/root-ca.crt`.
- Copy `device-cert.pem` to `certs/device-cert.pem`.
- Copy `device-key.pem` to `certs/device-key.pem`.

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

## Install And Run

```powershell
python -m pip install -r requirements.txt
python main.py
```

## Expected Console Output

```text
Protonest Connect Python cross-platform X.509 subscribe example
MQTT client_id == device name: exampledevice
No MQTT username/password is used for X.509 auth
Broker: mqtt.protonest.co:8883
Subscribe topic: protonest/exampledevice/state/motor
Ack topic: protonest/exampledevice/state/last-command
Root CA: ...\certs\root-ca.crt
Client cert: ...\certs\device-cert.pem
Client key: ...\certs\device-key.pem
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected: Success (0)
Subscribed to: protonest/exampledevice/state/motor
Incoming protonest/exampledevice/state/motor -> {"status":true}
Ack queued protonest/exampledevice/state/last-command -> {"status":true}
Ack published protonest/exampledevice/state/last-command -> {"status":true}
```

If this example works, move to [`../x509_ota`](../x509_ota/).
