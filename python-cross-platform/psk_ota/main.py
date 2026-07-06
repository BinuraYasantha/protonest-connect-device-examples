import config
from ota_runtime import OtaRuntime


def main():
    print("Protonest Connect Python cross-platform PSK OTA example")
    print("MQTT client_id == username == device name:", config.DEVICE_NAME)
    runtime = OtaRuntime()
    runtime.run()


if __name__ == "__main__":
    main()
