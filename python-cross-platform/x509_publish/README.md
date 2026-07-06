# x509_publish

`x509_publish` sends sample telemetry to a Protonest Connect `stream` topic using X.509 client certificate authentication.

Use this after `x509_connect` is working.

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
Protonest Connect Python cross-platform X.509 publish example
MQTT client_id == device name: exampledevice
No MQTT username/password is used for X.509 auth
Broker: mqtt.protonest.co:8883
Publish topic: protonest/exampledevice/stream/temperature
Root CA: ...\certs\root-ca.crt
Client cert: ...\certs\device-cert.pem
Client key: ...\certs\device-key.pem
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected: Success (0)
Published protonest/exampledevice/stream/temperature -> {"temperature_c": 24.5, "uptime_ms": 10000}
```

If this example publishes successfully, move to [`../x509_subscribe`](../x509_subscribe/).
