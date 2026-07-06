from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent

BROKER = "mqtt.protonest.co"
PORT = 8883

DEVICE_NAME = "YOUR_DEVICE_NAME"
MQTT_CLIENT_ID = DEVICE_NAME

STREAM_NAME = "temperature"
CA_CERT_PATH = BASE_DIR / "certs" / "root-ca.crt"
DEVICE_CERT_PATH = BASE_DIR / "certs" / "device-cert.pem"
DEVICE_KEY_PATH = BASE_DIR / "certs" / "device-key.pem"

CONNECT_TIMEOUT_SECONDS = 15
RECONNECT_DELAY_SECONDS = 5
PUBLISH_INTERVAL_SECONDS = 5
