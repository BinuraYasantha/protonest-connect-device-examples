import network
import ssl
import sys
import time

try:
    import ntptime
except ImportError:
    ntptime = None

from umqtt.simple import MQTTClient

import config


def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)

    try:
        wlan.config(pm=0xA11140)
    except Exception:
        pass

    if wlan.isconnected():
        print("Wi-Fi connected:", wlan.ifconfig())
        return wlan

    print("Connecting to Wi-Fi...")
    wlan.connect(config.WIFI_SSID, config.WIFI_PASSWORD)

    for attempt in range(config.WIFI_CONNECT_TIMEOUT_SECONDS):
        if wlan.isconnected():
            print("Wi-Fi connected:", wlan.ifconfig())
            return wlan

        print("Waiting for Wi-Fi...", attempt + 1)
        time.sleep(1)

    raise RuntimeError("Wi-Fi connection failed")


def sync_time():
    if ntptime is None:
        print("NTP not available")
        return

    try:
        print("Syncing time...")
        ntptime.settime()
        print("Time synced:", time.localtime())
    except Exception as exc:
        print("NTP sync failed:", exc)


def create_ssl_params():
    with open(config.MQTT_CA_DER_PATH, "rb") as cert_file:
        ca_der = cert_file.read()

    with open(config.DEVICE_CERT_PATH, "rb") as cert_file:
        cert_data = cert_file.read()

    with open(config.DEVICE_KEY_PATH, "rb") as key_file:
        key_data = key_file.read()

    return {
        "key": key_data,
        "cert": cert_data,
        "cadata": ca_der,
        "server_hostname": config.BROKER,
        "server_side": False,
        "cert_reqs": ssl.CERT_REQUIRED,
        "do_handshake": True,
    }


def connect_mqtt():
    ssl_params = create_ssl_params()

    client = MQTTClient(
        client_id=config.MQTT_CLIENT_ID.encode(),
        server=config.BROKER,
        port=config.PORT,
        user=None,
        password=None,
        keepalive=config.MQTT_KEEPALIVE_SECONDS,
        ssl=True,
        ssl_params=ssl_params,
    )

    print("Connecting to MQTT broker {}:{} as {}...".format(config.BROKER, config.PORT, config.DEVICE_NAME))
    client.connect()
    print("MQTT connected")
    return client


def main():
    print("Protonest Connect Pico W MicroPython X.509 connect example")
    print("MQTT client_id == device name:", config.DEVICE_NAME)
    print("No MQTT username/password is used for X.509 auth")

    while True:
        client = None
        wlan = None

        try:
            wlan = connect_wifi()
            sync_time()
            client = connect_mqtt()

            while True:
                client.ping()
                print(
                    "Status: wifi=connected mqtt=connected ip={} device={}".format(
                        wlan.ifconfig()[0],
                        config.DEVICE_NAME,
                    )
                )
                time.sleep(config.STATUS_PRINT_INTERVAL_SECONDS)

        except Exception as exc:
            print("X.509 connect example error:", exc)
            sys.print_exception(exc)

            try:
                if client is not None:
                    client.disconnect()
            except Exception:
                pass

            print("Reconnecting in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
            time.sleep(config.RECONNECT_DELAY_SECONDS)


main()
