import importlib.util
import json
import py_compile
import shutil
import ssl
import threading
import time
import traceback
import uuid

import paho.mqtt.client as mqtt
import requests

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


class AppContext:
    def __init__(self, runtime, stop_event, version):
        self._runtime = runtime
        self._stop_event = stop_event
        self.version = version
        self.device_name = config.DEVICE_NAME
        self.stream_name = config.STREAM_NAME
        self.publish_interval_seconds = config.PUBLISH_INTERVAL_SECONDS

    def mark_healthy(self):
        self._runtime.mark_worker_healthy()

    def publish_json(self, stream_name, payload):
        return self._runtime.publish_stream_json(stream_name, payload)

    def is_stopping(self):
        return self._stop_event.is_set()

    def sleep(self, seconds):
        deadline = time.monotonic() + seconds
        while not self._stop_event.is_set():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return True
            self._stop_event.wait(0.2 if remaining > 0.2 else remaining)
        return False


class WorkerRunner:
    def __init__(self, runtime):
        self._runtime = runtime
        self._thread = None
        self._stop_event = None
        self._healthy_event = None
        self._crashed_event = None
        self._crash_exception = None

    @property
    def crashed(self):
        return self._crashed_event is not None and self._crashed_event.is_set()

    def start(self, worker_path, version):
        self.stop()
        self._stop_event = threading.Event()
        self._healthy_event = threading.Event()
        self._crashed_event = threading.Event()
        self._crash_exception = None

        def target():
            try:
                module_name = "protonest_worker_{}".format(uuid.uuid4().hex)
                spec = importlib.util.spec_from_file_location(module_name, worker_path)
                if spec is None or spec.loader is None:
                    raise RuntimeError("Could not load worker from {}".format(worker_path))

                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)

                if not hasattr(module, "run"):
                    raise RuntimeError("Worker module must define run(app)")

                context = AppContext(self._runtime, self._stop_event, version)
                module.run(context)

                if not self._stop_event.is_set():
                    raise RuntimeError("Worker exited without a stop request")
            except Exception as exc:
                self._crash_exception = exc
                self._crashed_event.set()
                print("Application worker crashed:", exc)
                traceback.print_exc()

        print("Starting application worker version {}".format(version))
        self._thread = threading.Thread(target=target, daemon=True)
        self._thread.start()

    def mark_healthy(self):
        if self._healthy_event is not None and not self._healthy_event.is_set():
            print("Application marked healthy")
            self._healthy_event.set()

    def wait_healthy(self, timeout_seconds):
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            if self._healthy_event is not None and self._healthy_event.is_set():
                return True
            if self.crashed:
                return False
            time.sleep(0.1)
        return False

    def stop(self):
        if self._stop_event is None:
            return

        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=5)

        self._thread = None
        self._stop_event = None
        self._healthy_event = None
        self._crashed_event = None
        self._crash_exception = None


class OtaRuntime:
    def __init__(self):
        self.client = None
        self.connected_event = threading.Event()
        self.stop_event = threading.Event()
        self.ota_lock = threading.Lock()
        self.processing_ota_ids = set()
        self.worker = WorkerRunner(self)

    def validate_common_paths(self):
        config.CA_CERT_PATH.parent.mkdir(parents=True, exist_ok=True)
        config.DOWNLOADS_DIR.mkdir(parents=True, exist_ok=True)
        config.RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
        config.APP_CURRENT_DIR.mkdir(parents=True, exist_ok=True)
        config.APP_BACKUP_DIR.mkdir(parents=True, exist_ok=True)

        if not config.CA_CERT_PATH.exists():
            raise RuntimeError("Root CA file not found: {}".format(config.CA_CERT_PATH))
        if not config.HTTPS_CA_CERT_PATH.exists():
            raise RuntimeError("HTTPS OTA root CA file not found: {}".format(config.HTTPS_CA_CERT_PATH))
        if not config.APP_CURRENT_FILE.exists():
            raise RuntimeError("Application worker file not found: {}".format(config.APP_CURRENT_FILE))
        if config.DEVICE_NAME == "YOUR_DEVICE_NAME":
            raise RuntimeError("Set DEVICE_NAME in config.py before running this example.")
        if config.MQTT_CLIENT_ID != config.DEVICE_NAME:
            raise RuntimeError("MQTT_CLIENT_ID must match DEVICE_NAME for Protonest X.509 auth.")
        if not config.DEVICE_CERT_PATH.exists():
            raise RuntimeError("Device certificate file not found: {}".format(config.DEVICE_CERT_PATH))
        if not config.DEVICE_KEY_PATH.exists():
            raise RuntimeError("Device private key file not found: {}".format(config.DEVICE_KEY_PATH))

    def load_state(self):
        default_state = {
            "current_ota_id": None,
            "current_version": config.APP_VERSION,
            "previous_ota_id": None,
            "previous_version": None,
            "last_failed_ota": None,
            "last_failed_version": None,
            "last_completed_ota": None,
        }
        if not config.STATE_FILE.exists():
            self.save_state(default_state)
            return default_state.copy()
        try:
            state = json.loads(config.STATE_FILE.read_text(encoding="utf-8"))
        except Exception:
            print("State file was unreadable, recreating it")
            self.save_state(default_state)
            return default_state.copy()
        for key, value in default_state.items():
            state.setdefault(key, value)
        return state

    def save_state(self, state):
        temp_path = config.STATE_FILE.with_suffix(".tmp")
        temp_path.write_text(json.dumps(state, indent=2), encoding="utf-8")
        temp_path.replace(config.STATE_FILE)

    def build_stream_topic(self, stream_name):
        return "protonest/{}/stream/{}".format(config.DEVICE_NAME, stream_name)

    def ota_pending_topic(self):
        return "protonest/{}/ota/pending".format(config.DEVICE_NAME)

    def ota_status_topic(self):
        return "protonest/{}/ota/status/update".format(config.DEVICE_NAME)

    def build_client(self):
        client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=config.MQTT_CLIENT_ID,
        )
        client.on_connect = self.on_connect
        client.on_disconnect = self.on_disconnect
        client.on_message = self.on_message
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

    def on_connect(self, client, userdata, flags, reason_code, properties):
        print("MQTT connected:", format_reason_code(reason_code))
        self.connected_event.set()
        client.subscribe(self.ota_pending_topic(), qos=1)
        print("Subscribed to OTA topic:", self.ota_pending_topic())

    def on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties):
        print("MQTT disconnected:", format_reason_code(reason_code))
        self.connected_event.clear()

    def on_message(self, client, userdata, message):
        if message.topic != self.ota_pending_topic():
            return
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception as exc:
            print("Ignoring malformed OTA payload:", exc)
            return
        thread = threading.Thread(target=self.process_ota_command, args=(payload,), daemon=True)
        thread.start()

    def publish_stream_json(self, stream_name, payload):
        if self.client is None or not self.connected_event.is_set():
            print("Telemetry skipped because MQTT is not connected")
            return False
        topic = self.build_stream_topic(stream_name)
        payload_text = json.dumps(payload)
        info = self.client.publish(topic, payload_text, qos=1)
        info.wait_for_publish()
        if info.rc == mqtt.MQTT_ERR_SUCCESS:
            print("Published {} -> {}".format(topic, payload_text))
            return True
        print("Telemetry publish failed with rc={}".format(info.rc))
        return False

    def publish_ota_status(self, status, ota_id, reason=None):
        if self.client is None:
            return
        payload = {
            "status": status,
            "otaId": ota_id,
        }
        if reason:
            print("OTA status reason:", reason)
        payload_text = json.dumps(payload)
        info = self.client.publish(self.ota_status_topic(), payload_text, qos=1)
        info.wait_for_publish()
        print("Published OTA status {} -> {}".format(self.ota_status_topic(), payload_text))

    def mark_worker_healthy(self):
        self.worker.mark_healthy()

    def start_current_worker(self, version):
        self.worker.start(config.APP_CURRENT_FILE, version)
        return self.worker.wait_healthy(config.HEALTH_CHECK_TIMEOUT_SECONDS)

    def restart_backup_worker(self, version):
        if not config.APP_BACKUP_FILE.exists():
            return False
        shutil.copy2(config.APP_BACKUP_FILE, config.APP_CURRENT_FILE)
        self.worker.start(config.APP_CURRENT_FILE, version)
        return self.worker.wait_healthy(config.HEALTH_CHECK_TIMEOUT_SECONDS)

    def download_file(self, url, destination, expected_size):
        print("Downloading OTA file from {}".format(url))
        with requests.get(
            url,
            stream=True,
            timeout=(10, config.DOWNLOAD_TIMEOUT_SECONDS),
            verify=str(config.HTTPS_CA_CERT_PATH),
        ) as response:
            response.raise_for_status()
            with destination.open("wb") as handle:
                for chunk in response.iter_content(chunk_size=8192):
                    if chunk:
                        handle.write(chunk)
        actual_size = destination.stat().st_size
        print("Downloaded {} bytes to {}".format(actual_size, destination))
        if expected_size is not None and actual_size != expected_size:
            raise RuntimeError("Downloaded size mismatch {} != {}".format(actual_size, expected_size))

    def validate_python_file(self, file_path):
        py_compile.compile(str(file_path), doraise=True)
        print("Syntax validation passed for {}".format(file_path.name))

    def install_download(self, downloaded_file):
        config.APP_BACKUP_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copy2(config.APP_CURRENT_FILE, config.APP_BACKUP_FILE)
        shutil.copy2(downloaded_file, config.APP_CURRENT_FILE)
        print("Installed OTA worker to {}".format(config.APP_CURRENT_FILE))

    def process_ota_command(self, payload):
        try:
            # otaId, version, and url are required for cross-platform app-file OTA.
            # file_size is optional and is checked when Protonest includes it.
            ota_id = str(payload["otaId"]).strip()
            version = str(payload["version"]).strip()
            url = str(payload["url"]).strip()
            expected_size = int(payload["file_size"]) if payload.get("file_size") is not None else None
        except Exception as exc:
            print("Ignoring OTA payload with missing or invalid fields:", exc)
            return

        with self.ota_lock:
            if ota_id in self.processing_ota_ids:
                print("Ignoring otaId={} because it is already being processed".format(ota_id))
                return
            self.processing_ota_ids.add(ota_id)

        try:
            state = self.load_state()
            if state.get("current_ota_id") == ota_id:
                return
            if state.get("last_failed_ota") == ota_id:
                return
            if state.get("last_completed_ota") == ota_id:
                return

            print("Starting OTA workflow for otaId={} version={}".format(ota_id, version))
            download_path = config.DOWNLOADS_DIR / "{}_worker.py".format(ota_id)
            if download_path.exists():
                download_path.unlink()

            self.download_file(url, download_path, expected_size)
            self.validate_python_file(download_path)

            previous_version = state.get("current_version", config.APP_VERSION)
            previous_ota_id = state.get("current_ota_id")

            self.worker.stop()
            self.install_download(download_path)
            self.worker.start(config.APP_CURRENT_FILE, version)

            if not self.worker.wait_healthy(config.HEALTH_CHECK_TIMEOUT_SECONDS):
                print("Health check failed for otaId={}".format(ota_id))
                restore_ok = self.restart_backup_worker(previous_version)
                state["last_failed_ota"] = ota_id
                state["last_failed_version"] = version
                self.save_state(state)
                self.publish_ota_status("failed", ota_id, "health_check_failed" if restore_ok else "rollback_failed")
                return

            state["previous_ota_id"] = previous_ota_id
            state["previous_version"] = previous_version
            state["current_ota_id"] = ota_id
            state["current_version"] = version
            state["last_failed_ota"] = None
            state["last_failed_version"] = None
            state["last_completed_ota"] = ota_id
            self.save_state(state)
            self.publish_ota_status("completed", ota_id)
            print("OTA {} completed successfully".format(ota_id))
        except requests.RequestException as exc:
            print("OTA download failed:", exc)
            self.publish_ota_status("failed", ota_id, "download_failed")
        except py_compile.PyCompileError as exc:
            print("OTA syntax check failed:", exc)
            self.publish_ota_status("failed", ota_id, "syntax_check_failed")
        except Exception as exc:
            print("OTA failed unexpectedly:", exc)
            traceback.print_exc()
            self.publish_ota_status("failed", ota_id, "unexpected_error")
        finally:
            with self.ota_lock:
                self.processing_ota_ids.discard(ota_id)

    def run(self):
        self.validate_common_paths()
        state = self.load_state()
        print("Broker: {}:{}".format(config.BROKER, config.PORT))
        print("OTA pending topic:", self.ota_pending_topic())
        print("OTA status topic:", self.ota_status_topic())
        print("MQTT root CA:", config.CA_CERT_PATH)
        print("HTTPS OTA root CA:", config.HTTPS_CA_CERT_PATH)
        print("Current app file:", config.APP_CURRENT_FILE)
        print("State file:", config.STATE_FILE)
        print("Downloads dir:", config.DOWNLOADS_DIR)
        print("Health check timeout: {} seconds".format(config.HEALTH_CHECK_TIMEOUT_SECONDS))

        while not self.stop_event.is_set():
            self.client = self.build_client()
            try:
                print("Connecting to MQTT broker {}:{} as {}...".format(
                    config.BROKER,
                    config.PORT,
                    config.DEVICE_NAME,
                ))
                self.client.connect(config.BROKER, config.PORT, keepalive=60)
                self.client.loop_start()
                if not self.connected_event.wait(config.CONNECT_TIMEOUT_SECONDS):
                    raise TimeoutError("Timed out waiting for MQTT connection")
                if not self.start_current_worker(state.get("current_version", config.APP_VERSION)):
                    raise RuntimeError("Current worker did not become healthy at startup")
                print("OTA runtime is ready")

                while not self.stop_event.is_set():
                    if self.worker.crashed:
                        print("Current worker crashed. Restarting installed worker...")
                        state = self.load_state()
                        self.worker.start(config.APP_CURRENT_FILE, state.get("current_version", config.APP_VERSION))
                        if not self.worker.wait_healthy(config.HEALTH_CHECK_TIMEOUT_SECONDS):
                            raise RuntimeError("Installed worker failed to restart after crash")
                    time.sleep(0.5)
                return
            except KeyboardInterrupt:
                print("Stopping example")
                self.stop_event.set()
                return
            except Exception as exc:
                print("OTA runtime error:", exc)
            finally:
                self.worker.stop()
                self.connected_event.clear()
                try:
                    if self.client is not None:
                        self.client.disconnect()
                except Exception:
                    pass
                if self.client is not None:
                    self.client.loop_stop()

            print("Retrying in {} seconds...".format(config.RECONNECT_DELAY_SECONDS))
            time.sleep(config.RECONNECT_DELAY_SECONDS)
