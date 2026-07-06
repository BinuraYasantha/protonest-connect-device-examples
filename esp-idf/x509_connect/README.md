# x509_connect

`x509_connect` is the first ESP-IDF example to run when your Protonest Connect device uses X.509 client certificate authentication.

It verifies Wi-Fi, clock sync, TLS certificate validation, client certificate authentication, and MQTT connection to `mqtt.protonest.co:8883`.

## Files To Edit

Edit [`main/Config.h`](main/Config.h):

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define DEVICE_NAME "YOUR_DEVICE_NAME"
```

From the downloaded X.509 ZIP:

- Copy `root-ca.crt` into `main/certs/root-ca.crt`.
- Copy the device certificate into `main/certs/device-cert.pem`.
- Copy the device private key into `main/certs/device-key.pem`.

No MQTT username or MQTT password is needed for X.509 examples. The MQTT client ID is the device name, and the certificate/key pair authenticates the device.

The device certificate common name must match `DEVICE_NAME`.

## Build And Flash

```powershell
idf.py set-target esp32c6
idf.py build flash monitor
```

Change `esp32c6` to your ESP32 target when using a different ESP32 board.

## Expected Serial Output

```text
Protonest Connect ESP-IDF X.509 connect example
MQTT client_id == device name: exampledevice
Device certificate CN: exampledevice
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Syncing clock...
Clock synced: 1782905517
Connecting to MQTT broker mqtt.protonest.co:8883 as exampledevice
MQTT connected
Status: wifi=connected mqtt=connected rssi=-66 device=exampledevice
```

If this example connects successfully, move to [`../x509_publish`](../x509_publish/).
