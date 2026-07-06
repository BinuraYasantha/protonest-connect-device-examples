# x509_connect

`x509_connect` is the first cross-platform Python example to run when your Protonest Connect device uses X.509 client certificate authentication.

It verifies TLS certificate validation, client certificate authentication, and MQTT connection to `mqtt.protonest.co:8883`.

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

## Install And Run

```powershell
python -m pip install -r requirements.txt
python main.py
```

## Expected Console Output

```text
Protonest Connect Python cross-platform X.509 connect example
MQTT client_id == device name: exampledevice
No MQTT username/password is used for X.509 auth
Broker: mqtt.protonest.co:8883
Root CA: ...\certs\root-ca.crt
Client cert: ...\certs\device-cert.pem
Client key: ...\certs\device-key.pem
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected: Success (0)
Status: mqtt=connected broker=mqtt.protonest.co:8883 device=exampledevice
```

If this example connects successfully, move to [`../x509_publish`](../x509_publish/).
