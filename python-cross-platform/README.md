# Protonest Connect Python Cross-Platform Examples

This folder contains Protonest Connect examples for any machine that can run Python, including Windows, Linux, macOS, and Raspberry Pi SBC devices.

Each example is standalone. Open one folder, copy the required credential files into `certs/`, edit `config.py`, install the Python packages, and run `main.py`.

## Start Here

Use the PSK examples when your Protonest Connect device uses username/password credentials.

| Step | Example | Purpose |
| --- | --- | --- |
| 1 | [`psk_connect`](psk_connect/) | Verify TLS and MQTT login |
| 2 | [`psk_publish`](psk_publish/) | Publish telemetry to a `stream` topic |
| 3 | [`psk_subscribe`](psk_subscribe/) | Receive `state` commands and publish an ack |
| 4 | [`psk_ota`](psk_ota/) | Receive OTA requests, download a Python worker file, and test rollback |

Use the X.509 examples when your Protonest Connect device uses a client certificate and private key.

| Step | Example | Purpose |
| --- | --- | --- |
| 1 | [`x509_connect`](x509_connect/) | Verify TLS, client certificate auth, and MQTT connect |
| 2 | [`x509_publish`](x509_publish/) | Publish telemetry to a `stream` topic |
| 3 | [`x509_subscribe`](x509_subscribe/) | Receive `state` commands and publish an ack |
| 4 | [`x509_ota`](x509_ota/) | Receive OTA requests, download a Python worker file, and test rollback |

## Common Workflow

1. Open one example folder.
2. Copy the required files from the Protonest Connect credential ZIP into `certs/`.
3. Edit `config.py`.
4. Install dependencies:

```powershell
python -m pip install -r requirements.txt
```

5. Run the example:

```powershell
python main.py
```

The computer must already have network access. These examples do not manage Wi-Fi because they run on the host operating system, not directly on a Wi-Fi microcontroller.

## PSK Credentials

A PSK credential ZIP contains files like this:

```text
example_psk.zip
|-- root-ca.crt
|-- example-creds.txt
`-- http-root-ca.pem
```

Inside `example-creds.txt`:

```text
PSA Username: example
PSA Password: JvK82R17xQR!ZZx3K
```

For these examples:

```python
DEVICE_NAME = "example"
MQTT_USERNAME = DEVICE_NAME
MQTT_CLIENT_ID = DEVICE_NAME
MQTT_PASSWORD = "JvK82R17xQR!ZZx3K"
```

Copy `root-ca.crt` into the example's `certs/` folder. For OTA examples, also copy `http-root-ca.pem`.

## X.509 Credentials

An X.509 credential ZIP should provide:

```text
x509_credentials.zip
|-- root-ca.crt
|-- device-cert.pem
|-- device-key.pem
`-- http-root-ca.pem
```

Copy the required files into the example's `certs/` folder. X.509 examples do not use MQTT username or MQTT password. The MQTT client ID is the device name, and the client certificate/key pair authenticates the device.

Keep `DEVICE_NAME`, MQTT client ID, the registered device identity, and the client certificate identity aligned. The examples validate this where possible, and the broker will reject mismatched identities.

## Certificate Format

Python uses PEM files directly in these examples. Do not convert these files to DER for the cross-platform Python examples.

## MQTT Topic Rules

All examples follow the Protonest Connect MQTT topic rules:

- Level 1 is always `protonest`.
- Level 2 is always the device name.
- Level 3 must be `stream`, `state`, or `ota`.

The third level defines the feature area:

- `stream` is used for sensor data and telemetry-like messages.
- `state` is used for actuator state, device state, and command/operation flows.
- `ota` is used for OTA update request and status flows.

Example topics:

```text
protonest/<device>/stream/temperature
protonest/<device>/state/motor
protonest/<device>/state/last-command
protonest/<device>/ota/pending
protonest/<device>/ota/status/update
```

`protonest/<device>/state/last-command` is an example acknowledgement topic used by the subscription examples to confirm that an incoming command was received.

## OTA Notes

The Python OTA examples update `app_current/worker.py`. They do not update the operating system, Python interpreter, or machine firmware.

Rollback is application-level:

- The new worker must define `run(app)`.
- The new worker must call `app.mark_healthy()` after startup.
- If the worker fails before becoming healthy, the runtime restores `app_backup/worker.py`.
- OTA status is published to `protonest/<device>/ota/status/update` using only `status` and `otaId`.

The `downloads/` and `runtime/` folders are generated while OTA runs and are intentionally not committed.
