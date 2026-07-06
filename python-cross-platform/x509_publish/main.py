import json
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


def build_stream_topic():
    return "protonest/{}/stream/{}".format(config.DEVICE_NAME, config.STREAM_NAME)


def build_payload(start_time):
    uptime_ms = int((time.monotonic() - start_time) * 1000)
    sample_temperature = round(23.5 + ((uptime_ms // 1000) % 20) / 10, 1)
    return json.dumps(
        {
            "temperature_c": sample_temperature,
            "uptime_ms": uptime_ms,
        }
    )


def build_client(connected_event, ever_connected):
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=config.MQTT_CLIENT_ID,
    )

    def on_connect(client, userdata, flags, reason_code, properties):
        print("MQTT connected:", format_reason_code(reason_code))
        connected_event.set()
        ever_connected.set()

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
        if ever_connected.is_set():
            print("MQTT connection lost:", format_reason_code(reason_code))
            print("Network or broker became unavailable. A fresh reconnect will be attempted.")
        else:
            print("MQTT disconnected during connect:", format_reason_code(reason_code))
        connected_event.clear()

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
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

    topic = build_stream_topic()
    start_time = time.monotonic()

    print("Protonest Connect Python cross-platform X.509 publish example")
    print("MQTT client_id == device name:", config.DEVICE_NAME)
    print("No MQTT username/password is used for X.509 auth")
    print("Broker: {}:{}".format(config.BROKER, config.PORT))
    print("Publish topic:", topic)
    print("Root CA:", config.CA_CERT_PATH)
    print("Client cert:", config.DEVICE_CERT_PATH)
    print("Client key:", config.DEVICE_KEY_PATH)

    while True:
        connected_event = Event()
        ever_connected = Event()
        client = build_client(connected_event, ever_connected)

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
                payload = build_payload(start_time)
                publish_info = client.publish(topic, payload, qos=1)
                publish_info.wait_for_publish()

                if publish_info.rc != mqtt.MQTT_ERR_SUCCESS:
                    raise RuntimeError("Publish failed with rc={}".format(publish_info.rc))

                print("Published {} -> {}".format(topic, payload))
                time.sleep(config.PUBLISH_INTERVAL_SECONDS)

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
                print("X.509 publish example error after connection loss:", exc)
            else:
                print("X.509 publish example error during initial connect:", exc)

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
