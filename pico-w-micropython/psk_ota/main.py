import _thread
import machine
import sys
import time

import config
from ota import OtaManager


def run_app(manager):
    try:
        app_module = __import__("app")
        app_module.run(on_healthy=manager.mark_healthy)
    except Exception as exc:
        print("Application crashed:", exc)
        sys.print_exception(exc)
        time.sleep(config.APP_CRASH_REBOOT_DELAY_SECONDS)
        machine.reset()


def main():
    manager = OtaManager()
    print("Protonest Connect Pico W MicroPython OTA example")
    print("MQTT client_id == username == device name:", config.DEVICE_NAME)
    print("OTA pending topic:", manager._ota_pending_topic())
    print("OTA status topic:", manager._ota_status_topic())
    manager.connect_wifi()
    manager.sync_time()

    while True:
        try:
            manager.connect_mqtt()
            break
        except Exception as exc:
            print("MQTT connect failed:", exc)
            sys.print_exception(exc)
            time.sleep(config.RECONNECT_DELAY_SECONDS)

    manager.flush_status()
    manager.record_boot_attempt()
    rolled_back = manager.rollback_if_needed()
    manager.flush_status()

    if rolled_back:
        print("Rollback restored previous app. Rebooting...")
        time.sleep(1)
        machine.reset()

    _thread.start_new_thread(run_app, (manager,))

    while True:
        try:
            manager.ensure_wifi_connected()
            manager.service_mqtt()
        except Exception as exc:
            print("MQTT loop error:", exc)
            sys.print_exception(exc)
            manager.disconnect_mqtt()
            time.sleep(config.RECONNECT_DELAY_SECONDS)

        time.sleep(config.MQTT_POLL_INTERVAL_SECONDS)


main()
