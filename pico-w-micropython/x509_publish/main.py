import ssl
import sys
import time

try:
    import json
except ImportError:
    import ujson as json

import network

try:
    import ntptime
except ImportError:
    ntptime = None

from umqtt.simple import MQTTClient, MQTTException

import config


def build_stream_topic():
    return "protonest/{}/stream/{}".format(config.DEVICE_NAME, config.STREAM_NAME)


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


def create_mqtt_client():
    ssl_params = create_ssl_params()

    return MQTTClient(
        client_id=config.MQTT_CLIENT_ID.encode(),
        server=config.BROKER,
        port=config.PORT,
        user=None,
        password=None,
        keepalive=config.MQTT_KEEPALIVE_SECONDS,
        ssl=True,
        ssl_params=ssl_params,
    )


def validate_runtime_config():
    if config.DEVICE_NAME == "YOUR_DEVICE_NAME":
        raise RuntimeError(
            "Set DEVICE_NAME/MQTT_CLIENT_ID in config.py to the real device name registered on Protonest Connect. "
            "For X.509, the client ID must match the broker-side device identity."
        )


def build_payload(start_ticks_ms):
    uptime_ms = time.ticks_diff(time.ticks_ms(), start_ticks_ms)
    sample_temperature = 23.5 + ((uptime_ms // 1000) % 20) / 10

    return json.dumps(
        {
            "temperature_c": sample_temperature,
            "uptime_ms": uptime_ms,
        }
    )


def main():
    topic = build_stream_topic()
    start_ticks_ms = time.ticks_ms()

    print("Protonest Connect Pico W MicroPython X.509 publish example")
    print("MQTT client_id == device name:", config.DEVICE_NAME)
    print("No MQTT username/password is used for X.509 auth")
    print("Publish topic:", topic)

    while True:
        client = None

        try:
            validate_runtime_config()
            connect_wifi()
            sync_time()

            client = create_mqtt_client()
            print("Connecting to MQTT broker {}:{} as {}...".format(config.BROKER, config.PORT, config.DEVICE_NAME))
            client.connect()
            print("MQTT connected")

            while True:
                payload = build_payload(start_ticks_ms)
                client.publish(topic.encode(), payload.encode(), qos=1)
                print("Published {} -> {}".format(topic, payload))
                time.sleep(config.PUBLISH_INTERVAL_SECONDS)

        except MQTTException as exc:
            if len(exc.args) > 0 and exc.args[0] == 5:
                print("X.509 publish example error: broker refused connection: not authorized (code 5)")
                print(
                    "Check that DEVICE_NAME/MQTT_CLIENT_ID matches the registered device and the uploaded client certificate."
                )
            else:
                print("X.509 publish example error:", exc)
            sys.print_exception(exc)

            try:
                if client is not None:
                    client.disconnect()
            except Exception:
                pass

            print("Reconnecting in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
            time.sleep(config.RECONNECT_DELAY_SECONDS)

        except Exception as exc:
            print("X.509 publish example error:", exc)
            sys.print_exception(exc)

            try:
                if client is not None:
                    client.disconnect()
            except Exception:
                pass

            print("Reconnecting in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
            time.sleep(config.RECONNECT_DELAY_SECONDS)


main()
