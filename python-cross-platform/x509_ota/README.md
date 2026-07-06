# x509_ota

`x509_ota` is the cross-platform Python OTA example for Protonest Connect devices using X.509 client certificate authentication.

It listens for OTA requests over MQTT, downloads a Python worker file over HTTPS, installs it as `app_current/worker.py`, and rolls back to `app_backup/worker.py` if the new worker does not become healthy.

## Files To Edit

Edit [`config.py`](config.py):

```python
DEVICE_NAME = "YOUR_DEVICE_NAME"
```

From the downloaded X.509 credentials:

- Copy `root-ca.crt` to `certs/root-ca.crt`.
- Copy `device-cert.pem` to `certs/device-cert.pem`.
- Copy `device-key.pem` to `certs/device-key.pem`.
- Copy `http-root-ca.pem` to `certs/http-root-ca.pem`.

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

## Install And Run

```powershell
python -m pip install -r requirements.txt
python main.py
```

## Test OTA

1. Start this OTA example.
2. Open the Protonest Connect console.
3. Go to `Projects > Devices > Manage Device`.
4. Click `Send OTA Update`.
5. Select or drag and drop a Python worker file, such as [`test/normal_test_worker.py`](test/normal_test_worker.py).
6. Enter an OTA version, such as `1.1.0`.
7. Click `Send`.

After you click `Send`, the console treats the update as pending. The device publishes `completed` after the new worker calls `app.mark_healthy()`, or `failed` if download/install/rollback fails. Already-installed retained OTA requests are ignored silently.

## Test Rollback

Send [`test/rollback_test_worker.py`](test/rollback_test_worker.py) as an OTA file. That worker intentionally crashes before calling `app.mark_healthy()`. The OTA runtime restores the backup worker and publishes a failed status.

## Expected Console Output

```text
Protonest Connect Python cross-platform X.509 OTA example
MQTT client_id == device name: exampledevice
No MQTT username/password is used for X.509 auth
Broker: mqtt.protonest.co:8883
OTA pending topic: protonest/exampledevice/ota/pending
OTA status topic: protonest/exampledevice/ota/status/update
MQTT root CA: ...\certs\root-ca.crt
HTTPS OTA root CA: ...\certs\http-root-ca.pem
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice...
MQTT connected: Success (0)
Subscribed to OTA topic: protonest/exampledevice/ota/pending
Starting application worker version 0.0.1
X.509 default OTA worker version 0.0.1 started
Application marked healthy
OTA runtime is ready
Starting OTA workflow for otaId=351 version=1.1.0
Downloading OTA file from https://api.protonestconnect.co/api/v1/ota/download/351/exampledevice?token=replace-me
Downloaded 710 bytes to ...\downloads\351_worker.py
Syntax validation passed for 351_worker.py
Installed OTA worker to ...\app_current\worker.py
Starting application worker version 1.1.0
X.509 normal OTA test worker version 1.1.0 started
Application marked healthy
Published OTA status protonest/exampledevice/ota/status/update -> {"status": "completed", "otaId": "351"}
OTA 351 completed successfully
```
