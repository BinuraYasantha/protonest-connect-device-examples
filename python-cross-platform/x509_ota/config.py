from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent

BROKER = "mqtt.protonest.co"
PORT = 8883

DEVICE_NAME = "YOUR_DEVICE_NAME"
MQTT_CLIENT_ID = DEVICE_NAME

STREAM_NAME = "temperature"
PUBLISH_INTERVAL_SECONDS = 5
APP_VERSION = "0.0.1"

CA_CERT_PATH = BASE_DIR / "certs" / "root-ca.crt"
DEVICE_CERT_PATH = BASE_DIR / "certs" / "device-cert.pem"
DEVICE_KEY_PATH = BASE_DIR / "certs" / "device-key.pem"
HTTPS_CA_CERT_PATH = BASE_DIR / "certs" / "http-root-ca.pem"

APP_CURRENT_DIR = BASE_DIR / "app_current"
APP_CURRENT_FILE = APP_CURRENT_DIR / "worker.py"
APP_BACKUP_DIR = BASE_DIR / "app_backup"
APP_BACKUP_FILE = APP_BACKUP_DIR / "worker.py"
DOWNLOADS_DIR = BASE_DIR / "downloads"
RUNTIME_DIR = BASE_DIR / "runtime"
STATE_FILE = RUNTIME_DIR / "ota_state.json"

CONNECT_TIMEOUT_SECONDS = 15
RECONNECT_DELAY_SECONDS = 5
HEALTH_CHECK_TIMEOUT_SECONDS = 15
DOWNLOAD_TIMEOUT_SECONDS = 120
