# psk_connect

`psk_connect` is the first ESP-IDF example to run when your Protonest Connect device uses PSK username/password authentication.

It verifies Wi-Fi, TLS certificate validation, and MQTT login to `mqtt.protonest.co:8883`.

## Files To Edit

Edit [`main/Config.h`](main/Config.h):

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define DEVICE_NAME "YOUR_DEVICE_NAME"
#define MQTT_PASSWORD "YOUR_MQTT_PASSWORD"
```

From the downloaded PSK ZIP:

- Copy `root-ca.crt` into `main/certs/root-ca.crt`.
- Open the `*-creds.txt` file.
- Use `PSA Username` as `DEVICE_NAME`.
- Use `PSA Password` as `MQTT_PASSWORD`.

## Build And Flash

From this folder:

```powershell
idf.py set-target esp32c6
idf.py build flash monitor
```

Change `esp32c6` to your ESP32 target when using a different ESP32 board.

## Expected Serial Output

```text
Protonest Connect ESP-IDF PSK connect example
MQTT client_id == username == device name: exampledevice
Connecting to Wi-Fi...
Wi-Fi connected. IP: 192.168.1.26
Connecting to MQTT broker mqtt.protonest.co:8883
MQTT connected
```

If this example connects successfully, move to [`../psk_publish`](../psk_publish/).
