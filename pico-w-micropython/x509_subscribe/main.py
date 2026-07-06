import network
import ssl
import sys
import time

try:
    import ntptime
except ImportError:
    ntptime = None

from umqtt.simple import MQTTClient, MQTTException

import config


def build_state_topic(name):
    return "protonest/{}/state/{}".format(config.DEVICE_NAME, name)


def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)

    try:
        wlan.config(pm=0xA11140)
    except Exception:
        pass

    if wlan.isconnected():
        print("Wi-Fi connected:", wlan.ifconfig())
        return

    print("Connecting to Wi-Fi...")
    wlan.connect(config.WIFI_SSID, config.WIFI_PASSWORD)

    for attempt in range(config.WIFI_CONNECT_TIMEOUT_SECONDS):
        if wlan.isconnected():
            print("Wi-Fi connected:", wlan.ifconfig())
            return

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


def main():
    command_topic = build_state_topic(config.STATE_NAME)
    ack_topic = build_state_topic(config.ACK_STATE_NAME)
    client_box = {"client": None}

    def on_message(topic, msg):
        try:
            topic_text = topic.decode()
        except Exception:
            topic_text = str(topic)

        try:
            payload_text = msg.decode()
        except Exception:
            payload_text = str(msg)

        print("Incoming {} -> {}".format(topic_text, payload_text))

        if topic_text != command_topic:
            print("Message received on non-command topic, skipping ack")
            return

        client_box["client"].publish(ack_topic.encode(), msg, qos=1)
        print("Ack {} -> {}".format(ack_topic, payload_text))

    print("Protonest Connect Pico W MicroPython X.509 subscribe example")
    print("MQTT client_id == device name:", config.DEVICE_NAME)
    print("No MQTT username/password is used for X.509 auth")
    print("Subscribe topic:", command_topic)
    print("Ack topic:", ack_topic)

    while True:
        client = None

        try:
            connect_wifi()
            sync_time()

            client = create_mqtt_client()
            client.set_callback(on_message)
            client_box["client"] = client

            print("Connecting to MQTT broker {}:{} as {}...".format(config.BROKER, config.PORT, config.DEVICE_NAME))
            client.connect()
            print("MQTT connected")
            client.subscribe(command_topic.encode(), qos=1)
            print("Subscribed to:", command_topic)

            while True:
                client.check_msg()
                time.sleep(config.MQTT_POLL_INTERVAL_SECONDS)

        except MQTTException as exc:
            if len(exc.args) > 0 and exc.args[0] == 5:
                print("X.509 subscribe example error: broker refused connection: not authorized (code 5)")
                print(
                    "Check that DEVICE_NAME/MQTT_CLIENT_ID matches the registered device and the uploaded client certificate."
                )
            else:
                print("X.509 subscribe example error:", exc)
            sys.print_exception(exc)

            try:
                if client is not None:
                    client.disconnect()
            except Exception:
                pass

            print("Reconnecting in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
            time.sleep(config.RECONNECT_DELAY_SECONDS)

        except Exception as exc:
            print("X.509 subscribe example error:", exc)
            sys.print_exception(exc)

            try:
                if client is not None:
                    client.disconnect()
            except Exception:
                pass

            print("Reconnecting in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
            time.sleep(config.RECONNECT_DELAY_SECONDS)


main()
