import ssl
import sys
import time

try:
    import json
except ImportError:
    import ujson as json

try:
    import network
except ImportError:
    raise

try:
    import ntptime
except ImportError:
    ntptime = None

from umqtt.simple import MQTTClient

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


def create_ssl_context():
    with open(config.MQTT_CA_DER_PATH, "rb") as cert_file:
        ca_der = cert_file.read()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.verify_mode = ssl.CERT_REQUIRED
    ctx.load_verify_locations(cadata=ca_der)
    return ctx


def create_mqtt_client():
    return MQTTClient(
        client_id=config.MQTT_CLIENT_ID.encode(),
        server=config.BROKER,
        port=config.PORT,
        user=config.MQTT_USERNAME.encode(),
        password=config.MQTT_PASSWORD.encode(),
        keepalive=config.MQTT_KEEPALIVE_SECONDS,
        ssl=create_ssl_context(),
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

    print("Protonest Connect Pico W MicroPython publish example")
    print("MQTT client_id == username == device name:", config.DEVICE_NAME)
    print("Publish topic:", topic)

    while True:
        client = None

        try:
            connect_wifi()
            sync_time()

            client = create_mqtt_client()
            print("Connecting to MQTT broker {}:{}...".format(config.BROKER, config.PORT))
            client.connect()
            print("MQTT connected")

            while True:
                payload = build_payload(start_ticks_ms)
                client.publish(topic.encode(), payload.encode(), qos=1)
                print("Published {} -> {}".format(topic, payload))
                time.sleep(config.PUBLISH_INTERVAL_SECONDS)

        except Exception as exc:
            print("Publish example error:", exc)
            sys.print_exception(exc)

            try:
                if client is not None:
                    client.disconnect()
            except Exception:
                pass

            print("Reconnecting in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
            time.sleep(config.RECONNECT_DELAY_SECONDS)


main()
