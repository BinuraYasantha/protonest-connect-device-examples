# x509_ota

`x509_ota` is the Pico W MicroPython OTA example for Protonest Connect devices using X.509 client certificate authentication.

It listens for OTA requests over MQTT, downloads a Python app file over HTTPS, installs it as `/app.py`, and rolls back to `/app_prev.py` if the new app does not become healthy.

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
- Copy the HTTPS OTA root CA to `/certs/http-root-ca.der` on the Pico.

Convert the downloaded PEM files to DER first. See [Convert PEM Files To DER](../README.md#convert-pem-files-to-der).

No MQTT username or MQTT password is needed. The MQTT client ID is the device name.

## OTA Topics

The example subscribes to:

```text
protonest/<device>/ota/pending
```

The example publishes OTA status to:

```text
protonest/<device>/ota/status/update
```

Status payloads contain only `status` and `otaId`:

```json
{"status":"completed","otaId":"351"}
```

## Upload To Pico

Copy these files and folders to the Pico filesystem:

```text
main.py
config.py
ota.py
app.py
rollback_test_app.py
umqtt/
certs/root-ca.der
certs/device-cert.der
certs/device-key.der
certs/http-root-ca.der
```

Run `main.py` from Thonny, or save it as `/main.py` for automatic startup after reset.

## Test OTA

1. Start this OTA example on the Pico.
2. Prepare a Python app file to send, such as an edited `app.py`.
3. Open the Protonest Connect console.
4. Go to `Projects > Devices > Manage Device`.
5. Click `Send OTA Update`.
6. Select or drag and drop the Python app file.
7. Enter an OTA version, such as `1.1.0`.
8. Click `Send`.

After you click `Send`, the console treats the update as pending. The device publishes `completed` after the new `/app.py` calls `on_healthy()`, or `failed` if download/install/rollback fails. Already-installed retained OTA requests are ignored silently.

## Test Rollback

Send [`rollback_test_app.py`](rollback_test_app.py) as an OTA file. That app intentionally crashes before calling `on_healthy()`. After `MAX_PENDING_BOOTS`, the OTA manager restores `/app_prev.py`, reboots, and publishes a failed status.

## Expected Serial Output

```text
Protonest Connect Pico W MicroPython X.509 OTA example
MQTT client_id == device name: exampledevice
No MQTT username/password is used for X.509 auth
OTA pending topic: protonest/exampledevice/ota/pending
OTA status topic: protonest/exampledevice/ota/status/update
Connecting to Wi-Fi...
Wi-Fi connected: ('192.168.1.67', '255.255.255.0', '192.168.1.1', '192.168.1.1')
Syncing time...
Time synced: (2026, 6, 25, 12, 10, 10, 3, 176)
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
Subscribed to: protonest/exampledevice/ota/pending
Received OTA payload: {"otaId":351,"version":"1.1.0","file_size":710,"url":"https://api.protonestconnect.co/api/v1/ota/download/351/exampledevice?token=replace-me"}
Downloading from api.protonestconnect.co port 443
HTTP status: 200
Downloading firmware 100% (710/710)
OTA installed successfully
Rebooting into new app
Application started
Application health check in progress...
Application marked healthy
Published OTA status: {"status": "completed", "otaId": "351"}
```
