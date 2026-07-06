import ssl
import time
from threading import Event

import paho.mqtt.client as mqtt

import config


def format_reason_code(reason_code):
    if reason_code is None:
        return "unknown"

    try:
        if hasattr(reason_code, "value"):
            return "{} ({})".format(reason_code, reason_code.value)
    except Exception:
        pass

    return str(reason_code)


def _read_text(path):
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return ""


def validate_config():
    if config.DEVICE_NAME == "YOUR_DEVICE_NAME":
        raise RuntimeError("Set DEVICE_NAME in config.py before running this example.")

    if config.MQTT_CLIENT_ID != config.DEVICE_NAME:
        raise RuntimeError("MQTT_CLIENT_ID must match DEVICE_NAME for Protonest X.509 auth.")

    if not config.CA_CERT_PATH.exists():
        raise RuntimeError("Root CA file not found: {}".format(config.CA_CERT_PATH))

    if not config.DEVICE_CERT_PATH.exists():
        raise RuntimeError("Device certificate file not found: {}".format(config.DEVICE_CERT_PATH))

    if not config.DEVICE_KEY_PATH.exists():
        raise RuntimeError("Device private key file not found: {}".format(config.DEVICE_KEY_PATH))

    cert_text = _read_text(config.DEVICE_CERT_PATH)
    if "BEGIN CERTIFICATE" not in cert_text:
        raise RuntimeError(
            "Replace certs/device-cert.pem with your real device certificate before running this example."
        )

    key_text = _read_text(config.DEVICE_KEY_PATH)
    if "BEGIN PRIVATE KEY" not in key_text and "BEGIN RSA PRIVATE KEY" not in key_text:
        raise RuntimeError(
            "Replace certs/device-key.pem with your real device private key before running this example."
        )


def build_state_topic(name):
    return "protonest/{}/state/{}".format(config.DEVICE_NAME, name)


def build_client(connected_event, ever_connected, command_topic, ack_topic):
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=config.MQTT_CLIENT_ID,
    )
    pending_acks = {}

    def on_connect(client, userdata, flags, reason_code, properties):
        print("MQTT connected:", format_reason_code(reason_code))
        connected_event.set()
        ever_connected.set()
        client.subscribe(command_topic, qos=1)
        print("Subscribed to:", command_topic)

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
        if ever_connected.is_set():
            print("MQTT connection lost:", format_reason_code(reason_code))
            print("Network or broker became unavailable. A fresh reconnect will be attempted.")
        else:
            print("MQTT disconnected during connect:", format_reason_code(reason_code))
        connected_event.clear()

    def on_message(client, userdata, message):
        payload_text = message.payload.decode("utf-8", errors="replace")
        print("Incoming {} -> {}".format(message.topic, payload_text))

        if message.topic != command_topic:
            print("Message received on non-command topic, skipping ack")
            return

        publish_info = client.publish(ack_topic, message.payload, qos=1)
        if publish_info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError("Ack publish failed with rc={}".format(publish_info.rc))

        pending_acks[publish_info.mid] = (ack_topic, payload_text)
        print("Ack queued {} -> {}".format(ack_topic, payload_text))

    def on_publish(client, userdata, mid, reason_code, properties):
        pending_ack = pending_acks.pop(mid, None)
        if pending_ack is None:
            return

        topic, payload_text = pending_ack
        print("Ack published {} -> {}".format(topic, payload_text))

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.on_publish = on_publish
    client.reconnect_delay_set(min_delay=1, max_delay=config.RECONNECT_DELAY_SECONDS)
    client.tls_set(
        ca_certs=str(config.CA_CERT_PATH),
        certfile=str(config.DEVICE_CERT_PATH),
        keyfile=str(config.DEVICE_KEY_PATH),
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLS_CLIENT,
    )
    client.tls_insecure_set(False)
    return client


def main():
    validate_config()

    command_topic = build_state_topic(config.STATE_NAME)
    ack_topic = build_state_topic(config.ACK_STATE_NAME)

    print("Protonest Connect Python cross-platform X.509 subscribe example")
    print("MQTT client_id == device name:", config.DEVICE_NAME)
    print("No MQTT username/password is used for X.509 auth")
    print("Broker: {}:{}".format(config.BROKER, config.PORT))
    print("Subscribe topic:", command_topic)
    print("Ack topic:", ack_topic)
    print("Root CA:", config.CA_CERT_PATH)
    print("Client cert:", config.DEVICE_CERT_PATH)
    print("Client key:", config.DEVICE_KEY_PATH)

    while True:
        connected_event = Event()
        ever_connected = Event()
        client = build_client(connected_event, ever_connected, command_topic, ack_topic)

        try:
            print("Connecting to MQTT broker {}:{} as {}...".format(
                config.BROKER,
                config.PORT,
                config.DEVICE_NAME,
            ))
            client.connect(config.BROKER, config.PORT, keepalive=60)
            client.loop_start()

            if not connected_event.wait(config.CONNECT_TIMEOUT_SECONDS):
                raise TimeoutError("Timed out waiting for MQTT connection")

            while connected_event.is_set():
                time.sleep(config.POLL_INTERVAL_SECONDS)

            print("MQTT is no longer connected. Preparing a new connection attempt...")

        except KeyboardInterrupt:
            print("Stopping example")
            try:
                client.disconnect()
            except Exception:
                pass
            client.loop_stop()
            return

        except Exception as exc:
            if ever_connected.is_set():
                print("X.509 subscribe example error after connection loss:", exc)
            else:
                print("X.509 subscribe example error during initial connect:", exc)

        finally:
            try:
                client.disconnect()
            except Exception:
                pass
            client.loop_stop()

        print("Retrying in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
        time.sleep(config.RECONNECT_DELAY_SECONDS)


if __name__ == "__main__":
    main()
