import machine
import network
import ssl
import sys
import time

try:
    import json
except ImportError:
    import ujson as json

try:
    import ntptime
except ImportError:
    ntptime = None

try:
    import os
except ImportError:
    import uos as os

try:
    import socket
except ImportError:
    import usocket as socket

from umqtt.simple import MQTTClient

import config


class OtaManager:
    def __init__(self):
        self._mqtt_client = None
        self._wlan = network.WLAN(network.STA_IF)
        self._state = self._load_state()

    def connect_wifi(self):
        self._wlan.active(True)

        try:
            self._wlan.config(pm=0xA11140)
        except Exception:
            pass

        if self._wlan.isconnected():
            print("Wi-Fi connected:", self._wlan.ifconfig())
            return

        print("Connecting to Wi-Fi...")
        self._wlan.connect(config.WIFI_SSID, config.WIFI_PASSWORD)

        for attempt in range(config.WIFI_CONNECT_TIMEOUT_SECONDS):
            if self._wlan.isconnected():
                print("Wi-Fi connected:", self._wlan.ifconfig())
                return

            print("Waiting for Wi-Fi...", attempt + 1)
            time.sleep(1)

        raise RuntimeError("Wi-Fi connection failed")

    def ensure_wifi_connected(self):
        if not self._wlan.isconnected():
            self.connect_wifi()

    def sync_time(self):
        if ntptime is None:
            print("NTP not available")
            return

        try:
            print("Syncing time...")
            ntptime.settime()
            print("Time synced:", time.localtime())
        except Exception as exc:
            print("NTP sync failed:", exc)

    def connect_mqtt(self):
        self.disconnect_mqtt()

        client = MQTTClient(
            client_id=config.MQTT_CLIENT_ID.encode(),
            server=config.BROKER,
            port=config.PORT,
            user=config.MQTT_USERNAME.encode(),
            password=config.MQTT_PASSWORD.encode(),
            keepalive=config.MQTT_KEEPALIVE_SECONDS,
            ssl=self._create_mqtt_ssl_context(),
        )
        client.set_callback(self._on_mqtt_message)

        print("Connecting to MQTT broker {}:{}...".format(config.BROKER, config.PORT))
        client.connect()
        client.subscribe(self._ota_pending_topic().encode(), qos=1)
        print("Subscribed to:", self._ota_pending_topic())

        self._mqtt_client = client
        return client

    def disconnect_mqtt(self):
        if self._mqtt_client is None:
            return

        try:
            self._mqtt_client.disconnect()
        except Exception:
            pass

        self._mqtt_client = None

    def service_mqtt(self):
        if self._mqtt_client is None:
            self.connect_mqtt()

        self._mqtt_client.check_msg()
        self.flush_status()

    def flush_status(self):
        pending_status = self._state.get("status_to_publish")
        if not pending_status or self._mqtt_client is None:
            return

        payload = {
            "status": pending_status["status"],
            "otaId": pending_status["otaId"],
        }

        if pending_status.get("reason"):
            print("OTA status reason:", pending_status["reason"])

        payload_text = json.dumps(payload)
        self._mqtt_client.publish(
            self._ota_status_topic().encode(),
            payload_text.encode(),
            qos=1,
        )
        print("Published OTA status:", payload_text)

        self._state["status_to_publish"] = None
        self._state["last_status"] = pending_status["status"]
        self._state["last_status_ota_id"] = pending_status["otaId"]
        self._save_state()

    def record_boot_attempt(self):
        if not self._state.get("pending"):
            return

        self._state["boot_count"] = int(self._state.get("boot_count", 0)) + 1
        self._save_state()
        print(
            "Pending OTA boot count: {}/{}".format(
                self._state["boot_count"],
                config.MAX_PENDING_BOOTS,
            )
        )

    def rollback_if_needed(self):
        if not self._state.get("pending"):
            return False

        if int(self._state.get("boot_count", 0)) < config.MAX_PENDING_BOOTS:
            return False

        pending_ota_id = self._state.get("pending_ota_id")
        pending_version = self._state.get("pending_version")
        print("Rollback threshold reached for OTA", pending_ota_id)
        if not self._restore_backup():
            print("Rollback failed: no backup app available")
            self._state["last_failed_ota"] = pending_ota_id
            self._state["last_failed_version"] = pending_version
            self._clear_pending_state()
            self._queue_status("failed", pending_ota_id, "rollback_backup_missing")
            self._save_state()
            return False

        print("Previous app restored from app_prev.py")
        self._state["last_failed_ota"] = pending_ota_id
        self._state["last_failed_version"] = pending_version
        self._clear_pending_state()
        self._queue_status("failed", pending_ota_id, "rollback_restored")
        self._save_state()
        return True

    def mark_healthy(self):
        if not self._state.get("pending"):
            return

        pending_ota_id = self._state.get("pending_ota_id")
        pending_version = self._state.get("pending_version")

        print("Application marked healthy for OTA", pending_ota_id)

        self._state["previous_ota_id"] = self._state.get("current_ota_id")
        self._state["previous_version"] = self._state.get("current_version")
        self._state["current_ota_id"] = pending_ota_id
        self._state["current_version"] = pending_version or self._state.get(
            "current_version",
            config.APP_VERSION,
        )
        self._state["last_completed_ota"] = pending_ota_id
        self._state["last_failed_ota"] = None
        self._state["last_failed_version"] = None
        self._clear_pending_state()
        self._queue_status("completed", pending_ota_id, None)
        self._save_state()

    def _on_mqtt_message(self, topic, msg):
        try:
            topic_text = topic.decode()
        except Exception:
            topic_text = str(topic)

        if topic_text != self._ota_pending_topic():
            return

        try:
            payload_text = msg.decode()
            payload = json.loads(payload_text)
            print("Received OTA payload:", payload_text)
            self._handle_ota_payload(payload)
        except Exception as exc:
            print("Invalid OTA payload:", exc)
            sys.print_exception(exc)
            ota_id = None
            try:
                if isinstance(payload, dict):
                    ota_id = str(payload.get("otaId"))
            except Exception:
                pass
            self._queue_status("failed", ota_id, "invalid_payload")
            self._save_state()

    def _handle_ota_payload(self, payload):
        # otaId and url are required. version falls back to the current app
        # version, and file_size is checked only when Protonest includes it.
        ota_id = str(payload.get("otaId", "")).strip()
        version = str(payload.get("version", "")).strip()
        url = payload.get("url")
        file_size = payload.get("file_size")

        if not ota_id or not url:
            raise ValueError("OTA payload missing otaId or url")

        if self._state.get("pending") and self._state.get("pending_ota_id") == ota_id:
            return

        if self._state.get("current_ota_id") == ota_id:
            return

        if self._state.get("previous_ota_id") == ota_id:
            return

        if self._state.get("last_failed_ota") == ota_id:
            return

        if self._state.get("last_completed_ota") == ota_id:
            return

        if self._state.get("ota_in_progress"):
            print("Ignoring otaId={} because another OTA is already running".format(ota_id))
            return

        self._state["ota_in_progress"] = True
        self._save_state()

        temp_path = config.TEMP_DOWNLOAD_PATH
        self._remove_file(temp_path)

        try:
            size = self._download_file(url, temp_path)
            if size <= 0:
                raise RuntimeError("Downloaded file is empty")

            if file_size is not None and int(file_size) != size:
                raise RuntimeError("Downloaded size mismatch {} != {}".format(size, file_size))

            self._install_download(temp_path)
            self._state["pending"] = True
            self._state["boot_count"] = 0
            self._state["pending_ota_id"] = ota_id
            self._state["pending_version"] = version or self._current_device_version()
            self._state["last_status"] = "pending"
            self._state["ota_in_progress"] = False
            self._save_state()
            print("OTA installed successfully")
            print("Rebooting into new app")
            machine.reset()
        except Exception as exc:
            self._remove_file(temp_path)
            self._state["ota_in_progress"] = False
            self._queue_status("failed", ota_id, "ota_update_failed")
            self._save_state()
            print("OTA update failed:", exc)
            sys.print_exception(exc)

    def _download_file(self, url, destination_path):
        return self._download_with_redirects(url, destination_path, 0)

    def _download_with_redirects(self, url, destination_path, redirect_count):
        if redirect_count > config.MAX_HTTP_REDIRECTS:
            raise RuntimeError("Too many redirects")

        scheme, host, port, path = self._parse_url(url)
        sock = None

        try:
            print("Downloading from {} port {}".format(host, port))
            sock = self._open_socket(scheme, host, port)

            request = (
                "GET {} HTTP/1.1\r\n"
                "Host: {}\r\n"
                "Connection: close\r\n"
                "User-Agent: protonest-pico-ota/1.0\r\n"
                "\r\n"
            ).format(path, host)
            sock.write(request.encode())

            status_code, headers = self._read_headers(sock)
            print("HTTP status:", status_code)

            if status_code in (301, 302, 303, 307, 308):
                location = headers.get("location")
                if not location:
                    raise RuntimeError("Redirect without location header")

                redirect_url = self._build_absolute_redirect(scheme, host, port, location)
                return self._download_with_redirects(
                    redirect_url,
                    destination_path,
                    redirect_count + 1,
                )

            if status_code != 200:
                raise RuntimeError("Download failed with status {}".format(status_code))

            transfer_encoding = headers.get("transfer-encoding", "").lower()
            content_length_header = headers.get("content-length")
            content_length = int(content_length_header) if content_length_header else None

            with open(destination_path, "wb") as destination_file:
                if "chunked" in transfer_encoding:
                    return self._copy_chunked_response(sock, destination_file)
                return self._copy_response(sock, destination_file, content_length)
        finally:
            try:
                if sock is not None:
                    sock.close()
            except Exception:
                pass

    def _parse_url(self, url):
        if "://" not in url:
            raise ValueError("Invalid URL")

        scheme, remainder = url.split("://", 1)

        if "/" in remainder:
            host_port, path = remainder.split("/", 1)
            path = "/" + path
        else:
            host_port = remainder
            path = "/"

        if ":" in host_port:
            host, port_text = host_port.split(":", 1)
            port = int(port_text)
        else:
            host = host_port
            port = 443 if scheme == "https" else 80

        return scheme, host, port, path

    def _build_absolute_redirect(self, scheme, host, port, location):
        if "://" in location:
            return location

        if not location.startswith("/"):
            location = "/" + location

        default_port = 443 if scheme == "https" else 80
        if port == default_port:
            return "{}://{}{}".format(scheme, host, location)

        return "{}://{}:{}{}".format(scheme, host, port, location)

    def _open_socket(self, scheme, host, port):
        addr = socket.getaddrinfo(host, port)[0][-1]
        sock = socket.socket()

        try:
            sock.settimeout(config.DOWNLOAD_SOCKET_TIMEOUT_SECONDS)
        except Exception:
            pass

        sock.connect(addr)

        if scheme == "https":
            sock = self._create_https_ssl_context().wrap_socket(
                sock,
                server_hostname=host,
            )

        return sock

    def _read_headers(self, sock):
        status_line = sock.readline()
        if not status_line:
            raise RuntimeError("Empty HTTP response")

        parts = status_line.decode().strip().split()
        if len(parts) < 2:
            raise RuntimeError("Invalid HTTP status line")

        status_code = int(parts[1])
        headers = {}

        while True:
            line = sock.readline()
            if not line or line in (b"\r\n", b"\n"):
                break

            decoded = line.decode().strip()
            if ":" not in decoded:
                continue

            key, value = decoded.split(":", 1)
            headers[key.lower()] = value.strip()

        return status_code, headers

    def _copy_response(self, sock, destination_file, content_length):
        bytes_written = 0
        last_percent = -1

        while True:
            if content_length is not None:
                remaining = content_length - bytes_written
                if remaining <= 0:
                    break
                chunk = sock.read(512 if remaining > 512 else remaining)
            else:
                chunk = sock.read(512)

            if not chunk:
                break

            destination_file.write(chunk)
            bytes_written += len(chunk)

            if content_length:
                percent = (bytes_written * 100) // content_length
                if percent != last_percent:
                    print(
                        "Downloading firmware {}% ({}/{})".format(
                            percent,
                            bytes_written,
                            content_length,
                        )
                    )
                    last_percent = percent
            else:
                print("Downloaded {} bytes".format(bytes_written))

        return bytes_written

    def _copy_chunked_response(self, sock, destination_file):
        bytes_written = 0

        while True:
            chunk_size_line = sock.readline()
            if not chunk_size_line:
                raise RuntimeError("Unexpected end of chunked response")

            chunk_size_text = chunk_size_line.decode().strip().split(";", 1)[0]
            chunk_size = int(chunk_size_text, 16)

            if chunk_size == 0:
                sock.readline()
                break

            remaining = chunk_size
            while remaining > 0:
                chunk = sock.read(512 if remaining > 512 else remaining)
                if not chunk:
                    raise RuntimeError("Unexpected end of file")
                destination_file.write(chunk)
                remaining -= len(chunk)
                bytes_written += len(chunk)

            print("Downloaded {} bytes".format(bytes_written))
            sock.read(2)

        return bytes_written

    def _install_download(self, temp_path):
        app_path = config.APP_PATH
        backup_path = config.APP_BACKUP_PATH

        self._remove_file(backup_path)

        had_current_app = self._path_exists(app_path)
        if had_current_app:
            os.rename(app_path, backup_path)

        try:
            os.rename(temp_path, app_path)
        except Exception:
            if had_current_app and self._path_exists(backup_path):
                os.rename(backup_path, app_path)
            raise

    def _restore_backup(self):
        app_path = config.APP_PATH
        backup_path = config.APP_BACKUP_PATH

        if not self._path_exists(backup_path):
            print("No backup app available for rollback")
            return False

        self._remove_file(app_path)
        os.rename(backup_path, app_path)
        return True

    def _path_exists(self, path):
        try:
            os.stat(path)
            return True
        except OSError:
            return False

    def _remove_file(self, path):
        try:
            os.remove(path)
        except OSError:
            pass

    def _queue_status(self, status, ota_id, reason):
        if ota_id is None:
            return

        self._state["status_to_publish"] = {
            "status": status,
            "otaId": str(ota_id),
            "reason": reason,
        }

    def _create_mqtt_ssl_context(self):
        return self._create_ssl_context(config.MQTT_CA_DER_PATH)

    def _create_https_ssl_context(self):
        return self._create_ssl_context(config.HTTPS_CA_DER_PATH)

    def _create_ssl_context(self, ca_path):
        with open(ca_path, "rb") as cert_file:
            ca_der = cert_file.read()

        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(cadata=ca_der)
        return ctx

    def _ota_pending_topic(self):
        return "protonest/{}/ota/pending".format(config.DEVICE_NAME)

    def _ota_status_topic(self):
        return "protonest/{}/ota/status/update".format(config.DEVICE_NAME)

    def _current_device_version(self):
        current_version = self._state.get("current_version")
        if current_version:
            return current_version
        return config.APP_VERSION

    def _clear_pending_state(self):
        self._state["pending"] = False
        self._state["boot_count"] = 0
        self._state["pending_ota_id"] = None
        self._state["pending_version"] = None
        self._state["ota_in_progress"] = False

    def _default_state(self):
        return {
            "current_ota_id": None,
            "current_version": config.APP_VERSION,
            "previous_ota_id": None,
            "previous_version": None,
            "pending_ota_id": None,
            "pending_version": None,
            "last_failed_ota": None,
            "last_failed_version": None,
            "pending": False,
            "boot_count": 0,
            "last_status": None,
            "last_status_ota_id": None,
            "last_completed_ota": None,
            "status_to_publish": None,
            "ota_in_progress": False,
        }

    def _load_state(self):
        state = self._default_state()

        try:
            with open(config.OTA_STATE_PATH, "r") as state_file:
                loaded = json.loads(state_file.read())
            if isinstance(loaded, dict):
                state.update(loaded)
        except OSError:
            pass
        except Exception as exc:
            print("Failed to load OTA state:", exc)
            sys.print_exception(exc)

        return state

    def _save_state(self):
        with open(config.OTA_STATE_PATH, "w") as state_file:
            state_file.write(json.dumps(self._state))
