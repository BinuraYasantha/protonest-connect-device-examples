# Protonest Connect Pico W MicroPython Examples

This folder contains Protonest Connect examples for Raspberry Pi Pico W and Pico 2 W boards running MicroPython. Each example is a standalone folder that can be copied to the Pico filesystem and run immediately after editing `config.py`.

## Start Here

Use the PSK examples when your Protonest Connect device uses username/password credentials.

| Step | Example | Purpose |
| --- | --- | --- |
| 1 | [`psk_connect`](psk_connect/) | Verify Wi-Fi, TLS, and MQTT login |
| 2 | [`psk_publish`](psk_publish/) | Publish telemetry to a `stream` topic |
| 3 | [`psk_subscribe`](psk_subscribe/) | Receive `state` commands and publish an ack |
| 4 | [`psk_ota`](psk_ota/) | Receive OTA requests, download a Python app file, and test rollback |

Use the X.509 examples when your Protonest Connect device uses a client certificate and private key.

| Step | Example | Purpose |
| --- | --- | --- |
| 1 | [`x509_connect`](x509_connect/) | Verify Wi-Fi, TLS, client certificate auth, and MQTT connect |
| 2 | [`x509_publish`](x509_publish/) | Publish telemetry to a `stream` topic |
| 3 | [`x509_subscribe`](x509_subscribe/) | Receive `state` commands and publish an ack |
| 4 | [`x509_ota`](x509_ota/) | Receive OTA requests, download a Python app file, and test rollback |

## Common Workflow

1. Open one example folder in Thonny.
2. Edit `config.py`.
3. Copy the source files shown in the example README to the Pico filesystem.
4. Keep required folders such as `umqtt/` and `certs/` exactly as shown.
5. Run `main.py`.

Do not copy generated folders such as `__pycache__/`.

For automatic startup after reset, save the example's `main.py` on the Pico as `/main.py`.

## Authentication Model

For PSK examples, copy these values from the downloaded PSK credentials:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `DEVICE_NAME` from `PSA Username`
- `MQTT_PASSWORD` from `PSA Password`

For X.509 examples, copy these values from the downloaded X.509 credentials:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `DEVICE_NAME`, which must match the registered device/certificate identity
- `root-ca.der`
- `device-cert.der`
- `device-key.der`

X.509 examples do not use MQTT username or password. The MQTT client ID is the device name, and the client certificate/key pair authenticates the device.

Keep `DEVICE_NAME`, MQTT client ID, the registered device identity, and the client certificate identity aligned.

## Certificate Files

MicroPython TLS on Pico uses DER certificate/key files in these examples.
Protonest Connect downloads certificate files in PEM format, so convert them to DER before copying them to the Pico filesystem.

For PSK connect, publish, and subscribe examples:

```text
certs/
`-- root-ca.der
```

For PSK OTA examples:

```text
certs/
|-- root-ca.der
`-- http-root-ca.der
```

For X.509 connect, publish, and subscribe examples:

```text
certs/
|-- root-ca.der
|-- device-cert.der
`-- device-key.der
```

For X.509 OTA examples:

```text
certs/
|-- root-ca.der
|-- device-cert.der
|-- device-key.der
`-- http-root-ca.der
```

## Convert PEM Files To DER

Run these OpenSSL commands on your computer after downloading the Protonest Connect credential ZIP and before copying files to the Pico.

For PSK connect, publish, and subscribe examples:

```powershell
openssl x509 -in root-ca.pem -outform DER -out root-ca.der
```

For PSK OTA examples:

```powershell
openssl x509 -in root-ca.pem -outform DER -out root-ca.der
openssl x509 -in http-root-ca.pem -outform DER -out http-root-ca.der
```

For X.509 connect, publish, and subscribe examples:

```powershell
openssl x509 -in root-ca.pem -outform DER -out root-ca.der
openssl x509 -in device-cert.pem -outform DER -out device-cert.der
openssl rsa -in device-key.pem -outform DER -out device-key.der
```

For X.509 OTA examples:

```powershell
openssl x509 -in root-ca.pem -outform DER -out root-ca.der
openssl x509 -in device-cert.pem -outform DER -out device-cert.der
openssl rsa -in device-key.pem -outform DER -out device-key.der
openssl x509 -in http-root-ca.pem -outform DER -out http-root-ca.der
```

If your downloaded file uses `.crt` instead of `.pem`, use the same command and replace the input filename. The private key must be unencrypted for MicroPython TLS.

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

The MicroPython OTA examples update `/app.py`, not the Pico firmware image. The OTA manager downloads a Python file, backs up the current `/app.py` as `/app_prev.py`, installs the new app, and reboots.

Rollback is application-level:

- The new `app.py` must call `on_healthy()` after startup.
- If the app crashes before becoming healthy for `MAX_PENDING_BOOTS`, the manager restores `/app_prev.py`.
- OTA status is published to `protonest/<device>/ota/status/update` using only `status` and `otaId`.

Each OTA example folder includes `rollback_test_app.py` for rollback testing.
