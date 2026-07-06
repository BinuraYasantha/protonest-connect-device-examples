import config
from ota_runtime import OtaRuntime


def main():
    print("Protonest Connect Python cross-platform X.509 OTA example")
    print("MQTT client_id == device name:", config.DEVICE_NAME)
    print("No MQTT username/password is used for X.509 auth")
    runtime = OtaRuntime()
    runtime.run()


if __name__ == "__main__":
    main()
